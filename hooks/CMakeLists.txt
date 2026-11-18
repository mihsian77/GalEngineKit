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

# KiriKiri2 字体钩子（注入形态：自带 DllMain + GEK 导出）
add_library(krkr2_hook SHARED
    src/krkr2_hook.cpp
    src/font_clone.cpp
)
target_compile_definitions(krkr2_hook PRIVATE GEK_KRKR2_STANDALONE)

# version.dll 代理形态（免注入）：复用同一钩子核心 + version 导出转发。
# 不定义 GEK_KRKR2_STANDALONE，故 krkr2_hook.cpp 的 DllMain/GEK 导出被排除，
# 由 version_proxy.cpp 提供 DllMain。
add_library(version_proxy SHARED
    src/krkr2_hook.cpp
    src/font_clone.cpp
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