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

set_target_properties(example_hook PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    PREFIX ""  # 不添加 lib 前缀
)

if(MSVC)
    target_compile_options(example_hook PRIVATE /W4 /utf-8)
    set_property(TARGET example_hook PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
elseif(MINGW)
    target_compile_options(example_hook PRIVATE -Wall -Wextra)
    target_link_options(example_hook PRIVATE -static -static-libgcc -static-libstdc++)
endif()

target_link_libraries(example_hook PRIVATE kernel32 user32)

# 安装
install(TARGETS example_hook
    RUNTIME DESTINATION hooks
)

message(STATUS "GalEngineKit Hooks v${PROJECT_VERSION}")
