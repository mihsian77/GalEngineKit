# GalEngineKit - 顶层 CMake 构建脚本
# galgame 引擎适配与中文化工具箱
# 独立实现，MIT 许可

cmake_minimum_required(VERSION 3.15)
project(GalEngineKit VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 子项目
add_subdirectory(launcher)
add_subdirectory(hooks)

message(STATUS "========================================")
message(STATUS "GalEngineKit v${PROJECT_VERSION}")
message(STATUS "  启动器: launcher/")
message(STATUS "  钩子 DLL: hooks/")
message(STATUS "========================================")
