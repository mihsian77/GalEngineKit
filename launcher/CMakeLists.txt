# GalEngineKit Launcher - CMake 构建脚本
# 独立实现，MIT 许可

cmake_minimum_required(VERSION 3.15)
project(GalEngineKitLauncher VERSION 0.1.0 LANGUAGES CXX RC)

# C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源文件
set(SOURCES
    src/main.cpp
    src/logger.cpp
    src/config.cpp
    src/environment.cpp
    src/process.cpp
    src/pe.cpp
    src/injector.cpp
    src/patcher.cpp
    src/patch_library.cpp
    src/launcher.cpp
    src/engine_detector.cpp
    src/doctor.cpp
    resources/galenginekit.rc
)

# 头文件目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# RC 资源编译器包含路径（.rc 文件引用 icon.ico）
if(MINGW)
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I${CMAKE_CURRENT_SOURCE_DIR}/resources")
endif()

# 可执行文件
add_executable(GalEngineKitLauncher ${SOURCES})

# 编译选项
if(MSVC)
    target_compile_options(GalEngineKitLauncher PRIVATE /W4 /utf-8)
    # 静态链接 CRT（减少依赖）
    set_property(TARGET GalEngineKitLauncher PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
elseif(MINGW)
    target_compile_options(GalEngineKitLauncher PRIVATE -Wall -Wextra -Wno-unused-parameter)
    # 控制台子系统（入口点 wmain）+ 静态链接（减少依赖）
    target_link_options(GalEngineKitLauncher PRIVATE -mconsole -static -static-libgcc -static-libstdc++)
endif()

# 链接库（Windows 系统库）
target_link_libraries(GalEngineKitLauncher PRIVATE
    kernel32
    user32
    psapi
    shell32
)

# 输出目录
set_target_properties(GalEngineKitLauncher PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# 安装规则
install(TARGETS GalEngineKitLauncher
    RUNTIME DESTINATION bin
)

# 安装示例配置
install(FILES examples/GalEngineKit.ini.example
    DESTINATION share/galenginekit
    RENAME GalEngineKit.ini.example
)

# 打印构建信息
message(STATUS "========================================")
message(STATUS "GalEngineKit Launcher v${PROJECT_VERSION}")
message(STATUS "  编译器: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "  架构: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "  构建类型: ${CMAKE_BUILD_TYPE}")
message(STATUS "========================================")

# ---- 单元测试（默认不构建）----
option(GEK_BUILD_TESTS "构建 launcher 单元测试" OFF)
if(GEK_BUILD_TESTS)
    add_executable(test_patch_library
        tests/test_patch_library.cpp
        src/patch_library.cpp
        src/config.cpp
        src/logger.cpp
    )
    target_link_libraries(test_patch_library PRIVATE user32)
    if(MINGW)
        target_link_options(test_patch_library PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()
