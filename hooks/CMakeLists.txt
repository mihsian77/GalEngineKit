# GalEngineKit Hooks - CMake 构建脚本
# 独立实现，MIT 许可

cmake_minimum_required(VERSION 3.15)
project(GalEngineKitHooks VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 头文件目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 示例钩子 DLL
add_library(example_hook SHARED
    src/example_hook.cpp
)

# 钩子核心：两种交付形态共用
set(HOOK_CORE_SOURCES
    src/krkr2_hook.cpp      # 拦截函数 + 安装 + 导出
    src/font_clone.cpp      # 内存字体克隆
    src/hook_log.cpp        # 日志
    src/hook_config.cpp     # ini 与日文字体名单
    src/font_policy.cpp     # 反向伪装注册表 + 存在性探测
    src/iat_hook.cpp        # PE 导入表替换
)

# KiriKiri2 字体钩子（注入形态：自带 DllMain + GEK 导出）
add_library(krkr2_hook SHARED ${HOOK_CORE_SOURCES})
target_compile_definitions(krkr2_hook PRIVATE GEK_KRKR2_STANDALONE)

# version.dll 代理形态（免注入）：复用同一钩子核心 + version 导出转发。
# 不定义 GEK_KRKR2_STANDALONE，故 krkr2_hook.cpp 的 DllMain/GEK 导出被排除，
# 由 version_proxy.cpp 提供 DllMain。
add_library(version_proxy SHARED
    ${HOOK_CORE_SOURCES}
    src/version_proxy.cpp
)
set_target_properties(version_proxy PROPERTIES OUTPUT_NAME "version")

set_target_properties(example_hook krkr2_hook version_proxy PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    PREFIX ""  # 不添加 lib 前缀
)

if(MSVC)
    target_compile_options(example_hook PRIVATE /W4 /utf-8)
    target_compile_options(krkr2_hook PRIVATE /W4 /utf-8)
    target_compile_options(version_proxy PRIVATE /W4 /utf-8)
    set_property(TARGET example_hook PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    set_property(TARGET krkr2_hook PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    set_property(TARGET version_proxy PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    target_link_options(version_proxy PRIVATE
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/version_proxy.def")
elseif(MINGW)
    target_compile_options(example_hook PRIVATE -Wall -Wextra)
    target_compile_options(krkr2_hook PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_compile_options(version_proxy PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_link_options(example_hook PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_options(krkr2_hook PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_options(version_proxy PRIVATE -static -static-libgcc -static-libstdc++)
    # MinGW 直接把 .def 作为链接输入，映射公开导出名到内部包装符号
    target_link_options(version_proxy PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/version_proxy.def")
endif()

target_link_libraries(example_hook PRIVATE kernel32 user32)
# krkr2_hook 反向伪装用到 GetCurrentObject（gdi32）
target_link_libraries(krkr2_hook PRIVATE kernel32 user32 gdi32)
target_link_libraries(version_proxy PRIVATE kernel32 user32 gdi32)

# 安装
install(TARGETS example_hook krkr2_hook version_proxy
    RUNTIME DESTINATION hooks
)

message(STATUS "GalEngineKit Hooks v${PROJECT_VERSION}")
# ---- 回归测试（默认不构建）----
# test_font_clone 自带源字体入参，不依赖前缀里装没装字体；
# 其余三个走 LoadLibrary 加载真实 DLL，运行前要准备游戏目录结构。
option(GEK_BUILD_TESTS "构建 hooks 回归测试" OFF)
if(GEK_BUILD_TESTS)
    add_executable(test_font_clone tests/test_font_clone.cpp src/font_clone.cpp)
    add_executable(test_font_hook tests/test_font_hook.cpp)
    add_executable(test_version_proxy tests/test_version_proxy.cpp)
    add_executable(test_font_clone_hook tests/test_font_clone_hook.cpp)

    foreach(t test_font_clone test_font_hook test_version_proxy test_font_clone_hook)
        target_link_libraries(${t} PRIVATE kernel32 user32 gdi32)
        set_target_properties(${t} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
        if(MINGW)
            target_link_options(${t} PRIVATE -static -static-libgcc -static-libstdc++)
        endif()
    endforeach()
endif()
