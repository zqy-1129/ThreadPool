cmake_minimum_required(VERSION 3.10)

project(ThreadPool)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 动态库
add_library(ThreadPool SHARED
    ThreadPool.cpp
)

target_include_directories(ThreadPool
    PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ==================== 自动安装配置（核心）====================
# 安装动态库到 /usr/local/lib
install(TARGETS ThreadPool
    LIBRARY DESTINATION lib
)

# 安装头文件到 /usr/local/include
install(FILES ThreadPool.h
    DESTINATION include
)
# ==============================================================

# 3. 测试程序
add_executable(test test.cpp)
target_link_libraries(test ThreadPool)