# GalEngineKit Launcher - CMake 构建脚本
# 独立实现，MIT 许可

cmake_minimum_required(VERSION 3.15)
project(GalEngineKitLauncher VERSION 0.1.0 LANGUAGES CXX)

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
    src/launcher.cpp
)

# 头文件目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 可执行文件
add_executable(GalEngineKitLauncher ${SOURCES})

# 编译选项
if(MSVC)
    target_compile_options(GalEngineKitLauncher PRIVATE /W4 /utf-8)
    # 静态链接 CRT（减少依赖）
    set_property(TARGET GalEngineKitLauncher PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
elseif(MINGW)
    target_compile_options(GalEngineKitLauncher PRIVATE -Wall -Wextra -Wno-unused-parameter)
    # 静态链接（减少依赖）
    target_link_options(GalEngineKitLauncher PRIVATE -static -static-libgcc -static-libstdc++)
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
