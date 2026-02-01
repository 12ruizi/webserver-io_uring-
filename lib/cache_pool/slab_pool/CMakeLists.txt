# Slab内存池子模块
cmake_minimum_required(VERSION 3.10)
project(slab_pool VERSION 1.0.0 LANGUAGES CXX)

# 创建对象库
add_library(slab_pool_obj OBJECT
    slab_pool.cpp
    slab_pool.h
)

# 设置包含目录
target_include_directories(slab_pool_obj PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# 应用编译器选项
target_compile_options(slab_pool_obj PRIVATE -pthread)

# 独立的静态库（可选）
add_library(slab_pool_static STATIC
    $<TARGET_OBJECTS:slab_pool_obj>
)

target_include_directories(slab_pool_static PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

message(STATUS "Slab内存池模块配置完成")