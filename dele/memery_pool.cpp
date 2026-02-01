#include "../include/memery_pool.h"

// 构造函数实现
LayerMemoryPool::LayerMemoryPool(size_t max_connections, size_t cache_size,
                                 size_t min_block_size)
    : connection_pool(max_connections), cache_pool(cache_size, min_block_size) {
}

// 连接池管理接口实现
UringConnectionInfo *LayerMemoryPool::acquire_connection() {
  return connection_pool.acquire();
}

void LayerMemoryPool::release_connection(UringConnectionInfo *conn) {
  if (conn) {
    connection_pool.release(conn);
  }
}

// 缓存池管理接口实现
char *LayerMemoryPool::allocate_buffer(size_t size) {
  return cache_pool.allocate_buffer(size);
}

bool LayerMemoryPool::deallocate_buffer(char *ptr) {
  return cache_pool.deallocate_buffer(ptr);
}

void LayerMemoryPool::defragment_cache() { cache_pool.defragment(); }

// 监控和统计接口实现
LayerMemoryPool::CacheStats LayerMemoryPool::get_cache_stats() {
  CacheStats stats;
  stats.fragmentation = cache_pool.get_fragmentation();
  stats.available_memory = cache_pool.get_available_memory();
  stats.total_memory = MAX_CACHE_SIZE;
  return stats;
}

void LayerMemoryPool::print_connection_stats() {
  connection_pool.print_stats();
}

// 状态显示接口实现
void LayerMemoryPool::print_status() {
  std::cout << "=== 分层内存池状态 ===" << std::endl;

  // 连接池状态
  std::cout << "连接池状态:" << std::endl;
  connection_pool.print_stats();

  // 缓存池状态
  auto cache_stats = get_cache_stats();
  std::cout << "缓存池: " << cache_stats.available_memory << "/"
            << cache_stats.total_memory << " 字节可用" << std::endl;
  std::cout << "碎片率: " << cache_stats.fragmentation << "%" << std::endl;

  std::cout << "======================" << std::endl;
}

// 健康检查接口实现
std::string LayerMemoryPool::health_check() {
  auto cache_stats = get_cache_stats();

  if (cache_stats.available_memory < cache_stats.total_memory * 0.1) {
    return "WARNING: 缓存池内存不足";
  }

  if (cache_stats.fragmentation > 50) {
    return "WARNING: 缓存碎片率过高";
  }

  return "HEALTHY: 内存池运行正常";
}