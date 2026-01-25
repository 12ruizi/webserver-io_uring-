cmake_minimum_required(VERSION 3.10)
project(io_uring_server VERSION 1.0.0)

# C++标准设置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 生成编译命令数据库
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 源文件列表
set(SOURCES
    src/main.cpp
)
# 头文件列表
set(HEADERS
    include/funC.h
    include/http.h
    include/http_complete.h
    include/pthread_pool.h
    include/tcp.h
    include/uring.h
    include/worker.h
    include/taskHander.h
    include/dispacher.h
)


# 添加可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# 包含目录
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# 编译器选项
target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -g
    -O0
    -D_GNU_SOURCE
    -pthread
)

# 链接库
target_link_libraries(${PROJECT_NAME} PRIVATE 
    pthread 
    uring
    stdc++fs
)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 查找liburing库
find_library(LIBURING uring)
if(NOT LIBURING)
    message(WARNING "liburing库未找到，请确保已安装liburing-dev包")
endif()

# 安装目标
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# 打印配置信息
message(STATUS "项目名称: ${PROJECT_NAME}")
message(STATUS "C++标准: ${CMAKE_CXX_STANDARD}")
message(STATUS "包含目录: ${CMAKE_SOURCE_DIR}/include")
message(STATUS "源文件: ${SOURCES}")