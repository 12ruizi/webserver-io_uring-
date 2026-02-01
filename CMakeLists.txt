cmake_minimum_required(VERSION 3.10)
project(io_uring_server VERSION 1.0.0 LANGUAGES CXX)

# C++标准设置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 生成编译命令数据库
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 编译器选项（全局设置）
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -g -O0 -D_GNU_SOURCE -pthread")

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 查找liburing库
find_library(LIBURING_LIBRARY NAMES uring)
if(NOT LIBURING_LIBRARY)
    message(FATAL_ERROR "liburing库未找到，请安装liburing-dev包")
endif()

# 包含目录设置
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/dele
)

# 1. 缓存池模块 (cache_pool)
add_library(cache_pool STATIC
    lib/cache_pool/buddy_pool/buddy_pool.cpp
    lib/cache_pool/buddy_pool/buddy_pool.h
    lib/cache_pool/slab_pool/slab_pool.h
)

target_include_directories(cache_pool PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib/cache_pool
)

# 主可执行文件
add_executable(${PROJECT_NAME} 
    src/main.cpp
    src/uring_server.cpp
   
    src/http_complete.cpp
)

# 包含目录
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib/cache_pool
    ${CMAKE_SOURCE_DIR}/dele
)

# 链接所有库
target_link_libraries(${PROJECT_NAME} PRIVATE 
    cache_pool
    pthread 
    ${LIBURING_LIBRARY}
)

# 安装目标
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
install(TARGETS cache_pool DESTINATION lib)

# 打印配置信息
message(STATUS "项目名称: ${PROJECT_NAME}")
message(STATUS "C++标准: ${CMAKE_CXX_STANDARD}")
message(STATUS "liburing库: ${LIBURING_LIBRARY}")
message(STATUS "模块化编译: 启用")
message(STATUS "包含目录: ${CMAKE_SOURCE_DIR}/include 和所有lib子目录")