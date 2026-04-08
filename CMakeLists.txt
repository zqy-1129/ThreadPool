cmake_minimum_required(VERSION 3.10)

project(ThreadPool)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 头文件-only 库，不需要编译，只需要声明接口
add_library(ThreadPool INTERFACE)

# 指定头文件路径（INTERFACE 表示给使用这个库的目标提供头文件）
target_include_directories(ThreadPool
    INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 安装：只需要安装头文件
install(FILES ThreadPool.h
    DESTINATION include
)

# 测试程序
add_executable(test test.cpp)
target_link_libraries(test ThreadPool)