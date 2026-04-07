cmake_minimum_required(VERSION 3.10)

project(ThreadPool)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 静态库
add_library(ThreadPool STATIC
    ThreadPool.cpp
    ThreadPool.h
)

add_executable(test test.cpp)

target_link_libraries(test ThreadPool)