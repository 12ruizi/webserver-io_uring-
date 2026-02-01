#pragma once

// C++标准库头文件
#include <iostream>
#include <string>

// 项目内头文件 - 集成lib中的组件
#include "../lib/cache_pool/buddy_pool/buddy_pool.h"
#include "../lib/cache_pool/slab_pool/slab_pool.h"
#include "uring_types.h"

// 内存池分层设计 - 集成lib中的缓存池组件
class LayerMemoryPool {
private:
  // 连接对象池 - 使用slab池管理连接对象
  SlabConnectionPool<UringConnectionInfo> connection_pool;

  // 缓冲区内存池 - 使用buddy池管理内存缓存
  Buffer_BuddySystem cache_pool;

public:
  // 构造函数
  LayerMemoryPool(size_t max_connections = URING_MAX_CONNECTIONS,
                  size_t cache_size = MAX_CACHE_SIZE,
                  size_t min_block_size = MIN_BLOCK_SIZE)
      : connection_pool(max_connections),
        cache_pool(cache_size, min_block_size) {}

  // 连接池管理接口
  UringConnectionInfo *acquire_connection() {
    return connection_pool.acquire();
  }

  void release_connection(UringConnectionInfo *conn) {
    if (conn) {
      connection_pool.release(conn);
    }
  }

  // 缓存池管理接口
  char *allocate_buffer(size_t size) {
    return cache_pool.allocate_buffer(size);
  }

  bool deallocate_buffer(char *ptr) {
    return cache_pool.deallocate_buffer(ptr);
  }

  void defragment_cache() { cache_pool.defragment(); }

  // 对象池管理接口
  template <typename ObjectType>
  ObjectType *allocate_objects(size_t count = 1) {
    size_t total_size = sizeof(ObjectType) * count;
    char *buffer = allocate_buffer(total_size);
    if (!buffer)
      return nullptr;

    // 在分配的内存上构造对象
    ObjectType *objects = reinterpret_cast<ObjectType *>(buffer);
    for (size_t i = 0; i < count; ++i) {
      new (&objects[i]) ObjectType();
    }
    return objects;
  }

  template <typename ObjectType>
  void deallocate_objects(ObjectType *objects, size_t count = 1) {
    if (!objects)
      return;

    // 调用析构函数
    for (size_t i = 0; i < count; ++i) {
      objects[i].~ObjectType();
    }

    // 释放内存
    deallocate_buffer(reinterpret_cast<char *>(objects));
  }

  // 监控和统计接口
  struct CacheStats {
    size_t fragmentation;    // 碎片率
    size_t available_memory; // 可用内存
    size_t total_memory;     // 总内存
  };

  CacheStats get_cache_stats() {
    CacheStats stats;
    stats.fragmentation = cache_pool.get_fragmentation();
    stats.available_memory = cache_pool.get_available_memory();
    stats.total_memory = cache_pool.get_available_memory() +
                         (cache_pool.get_fragmentation() *
                          cache_pool.get_available_memory() / 100);
    return stats;
  }

  void print_connection_stats() { connection_pool.print_stats(); }

  // 状态显示接口
  void print_status() {
    std::cout << "=== Layer Memory Pool Status ===" << std::endl;
    CacheStats stats = get_cache_stats();
    std::cout << "Cache Fragmentation: " << stats.fragmentation << "%"
              << std::endl;
    std::cout << "Available Memory: " << stats.available_memory << " bytes"
              << std::endl;
    std::cout << "Total Memory: " << stats.total_memory << " bytes"
              << std::endl;
    std::cout << "=================================" << std::endl;
  }

  // 健康检查接口
  std::string health_check() {
    CacheStats stats = get_cache_stats();
    if (stats.fragmentation > 80) {
      return "WARNING: High fragmentation detected";
    }
    if (stats.available_memory < stats.total_memory * 0.1) {
      return "WARNING: Low available memory";
    }
    return "HEALTHY: Memory pool is operating normally";
  }
};