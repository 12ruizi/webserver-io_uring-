# Buddy内存池子模块
cmake_minimum_required(VERSION 3.10)
project(buddy_pool VERSION 1.0.0 LANGUAGES CXX)

# 创建对象库（便于聚合）
add_library(buddy_pool_obj OBJECT
    buddy_pool.cpp
    buddy_pool.h
)

# 设置包含目录
target_include_directories(buddy_pool_obj PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# 应用编译器选项
target_compile_options(buddy_pool_obj PRIVATE -pthread)

# 也可以创建独立的静态库（可选）
add_library(buddy_pool_static STATIC
    $<TARGET_OBJECTS:buddy_pool_obj>
)

target_include_directories(buddy_pool_static PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

message(STATUS "Buddy内存池模块配置完成")