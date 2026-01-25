#pragma once
#include "funC.h"
//固定大小的连接内存池
class FixedConnectionPool {
private:
  struct PooledConnection {
    Info_net_s conn;
    bool in_use;
    uint64_t last_used;
    PooledConnection() : in_use(false), last_used(0) {}
  };
  std::vector<PooledConnection> connections;
  std::mutex pool_mutex;
  size_t max_connections;
  std::atomic<size_t> active_connections{0};

public:
  FixedConnectionPool(size_t max_connections)
      : max_connections(max_connections) {
    connections.resize(max_connections);
  }
  //获取连接 （固定大小）
  Info_net_s *acquire(int fd) {
    std::lock_guard<std::mutex> lock(pool_mutex);
    if (active_connections >= max_connections) {
      //连接池数量达到上限，尝试回收最久未使用的连接
      if (!recycle_oldest_connection()) {
        return nullptr;
      }
    }
    // 查找空闲连接
    for (auto &pooled_conn : connections) {
      if (!pooled_conn.in_use) {
        pooled_conn.in_use = true;
        pooled_conn.last_used = get_current_time();
        pooled_conn.conn.fd = fd;
        active_connections++;
        return &pooled_conn.conn;
      }
    }
    return nullptr;
  }

  //释放连接 （固定大小）
  void release(Info_net_s *conn) {
    std::lock_guard<std::mutex> lock(pool_mutex);
    for (auto &pooled_conn : connections) {
      if (&pooled_conn.conn == conn) {
        pooled_conn.in_use = false;
        pooled_conn.last_used = 0;
        active_connections--;
        break;
      }
    }
  }
  //获取统计信息
  struct Stats {
    size_t total;
    size_t active;
    size_t available;
  };
  Stats get_stats() const {
    Stats stats;
    stats.total = max_connections;
    stats.active = active_connections.load();
    stats.available = max_connections - active_connections.load();
    return stats;
  }

private:
  bool recycle_oldest_connection() {
    uint64_t oldest_time = UINT64_MAX;
    PooledConnection *oldest_conn = nullptr;
    for (auto &pooled_conn : connections) {
      if (pooled_conn.in_use && pooled_conn.last_used < oldest_time) {
        oldest_time = pooled_conn.last_used;
        oldest_conn = &pooled_conn;
      }
    }
    if (oldest_conn) {
      oldest_conn->in_use = false;
      // 关闭连接
      close(oldest_conn->conn.fd);
      oldest_conn->conn.fd = -1;
      oldest_conn->last_used = 0;
      active_connections--;
      return true;
    }
    return false;
  }
  uint64_t get_current_time() const {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
  }
};