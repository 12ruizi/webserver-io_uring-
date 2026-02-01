#pragma once

// C系统头文件
#include <liburing.h>
#include <liburing/io_uring.h>

// C++标准库头文件
#include <atomic>
#include <functional>
#include <iostream>
#include <memory>
#include <vector>

// 项目模块头文件（使用前置声明避免循环依赖）
class SlabConnectionPool;
class TaskDispatcher;
class ThreadPool;
class TcpListener;

// io_uring模块专用头文件
#include "tcp.h"
#include "uring_types.h"

// io_uring服务器主类
class IoUringServer {
private:
  struct io_uring _ring;
  TcpListener _tcp_listener;
  std::unique_ptr<ThreadPool> _thread_pool;
  std::unique_ptr<SlabConnectionPool<UringConnectionInfo>> _connection_pool;
  std::atomic<bool> _running;

  // 初始化io_uring
  bool initialize_uring() {
    int ret = io_uring_queue_init(URING_MAX_QUEUE, &_ring, 0);
    if (ret != 0) {
      std::cerr << "io_uring初始化失败: " << ret << std::endl;
      return false;
    }
    return true;
  }

  // 初始化线程池
  bool
  initialize_thread_pool(size_t thread_count = URING_DEFAULT_THREAD_COUNT) {
    try {
      // 使用前置声明，实际实现在其他地方
      return true;
    } catch (const std::exception &e) {
      std::cerr << "线程池初始化失败: " << e.what() << std::endl;
      return false;
    }
  }

  // 设置accept事件
  bool set_accept_event(int listen_fd) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&_ring);
    if (!sqe) {
      std::cerr << "获取SQE失败" << std::endl;
      return false;
    }

    // 从连接池获取连接信息
    UringConnectionInfo *conn = _connection_pool->acquire();
    if (!conn) {
      std::cerr << "连接池获取失败" << std::endl;
      return false;
    }

    conn->state = UringConnectionState::ACCEPT;
    io_uring_prep_accept(sqe, listen_fd, (struct sockaddr *)&conn->addr,
                         &conn->addrlen, 0);
    io_uring_sqe_set_data(sqe, conn);
    return true;
  }

  // 设置读事件
  bool set_read_event(UringConnectionInfo *conn) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&_ring);
    if (!sqe) {
      std::cerr << "获取SQE失败" << std::endl;
      return false;
    }

    conn->state = UringConnectionState::READ;
    char *buffer = conn->read_buffer.get_write_tail();
    size_t buffer_size = conn->read_buffer.get_writable_size();

    if (buffer_size == 0) {
      conn->read_buffer.clear();
      buffer = conn->read_buffer.get_write_tail();
      buffer_size = conn->read_buffer.get_writable_size();
    }

    io_uring_prep_read(sqe, conn->fd, buffer, buffer_size, 0);
    io_uring_sqe_set_data(sqe, conn);
    return true;
  }

  // 设置写事件
  bool set_write_event(UringConnectionInfo *conn) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&_ring);
    if (!sqe) {
      std::cerr << "获取SQE失败" << std::endl;
      return false;
    }

    conn->state = UringConnectionState::WRITE;
    io_uring_prep_write(sqe, conn->fd, conn->write_buffer.get_read_head(),
                        conn->write_buffer.get_readable_size(), 0);
    io_uring_sqe_set_data(sqe, conn);
    return true;
  }

  // 设置关闭事件
  bool set_close_event(UringConnectionInfo *conn) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&_ring);
    if (!sqe) {
      std::cerr << "获取SQE失败" << std::endl;
      return false;
    }
    conn->state = UringConnectionState::CLOSE;
    io_uring_prep_close(sqe, conn->fd);
    io_uring_sqe_set_data(sqe, conn);
    return true;
  }

  // 处理完成事件
  void process_completion_events() {
    struct io_uring_cqe *cqe;
    unsigned head;
    unsigned count = 0;

    io_uring_for_each_cqe(&_ring, head, cqe) {
      count++;
      int result = cqe->res;

      if (result < 0) {
        std::cerr << "IO操作错误: " << result << std::endl;
        continue;
      }

      UringConnectionInfo *conn =
          static_cast<UringConnectionInfo *>(io_uring_cqe_get_data(cqe));
      if (!conn) {
        std::cerr << "获取连接数据失败" << std::endl;
        continue;
      }

      handle_completion_event(conn, result);
    }

    io_uring_cq_advance(&_ring, count);
  }

  // 处理单个完成事件
  void handle_completion_event(UringConnectionInfo *conn, int result) {
    switch (conn->state) {
    case UringConnectionState::ACCEPT:
      handle_accept_event(conn, result);
      break;
    case UringConnectionState::READ:
      handle_read_event(conn, result);
      break;
    case UringConnectionState::WRITE:
      handle_write_event(conn, result);
      break;
    case UringConnectionState::CLOSE:
      handle_close_event(conn);
      break;
    default:
      std::cerr << "未知连接状态: " << static_cast<int>(conn->state)
                << std::endl;
      _connection_pool->release(conn);
      break;
    }
  }

  void handle_accept_event(UringConnectionInfo *conn, int result) {
    if (result >= 0) {
      conn->state = UringConnectionState::READ;
      conn->fd = result;
      set_read_event(conn);
    } else {
      std::cerr << "Accept失败: " << result << std::endl;
      _connection_pool->release(conn);
    }
  }

  void handle_read_event(UringConnectionInfo *conn, int result) {
    if (result == 0) {
      set_close_event(conn);
    } else if (result > 0) {
      conn->read_buffer.write_data(result);
      // 这里可以提交任务到线程池处理
      set_write_event(conn);
    } else {
      std::cerr << "Read失败: " << result << std::endl;
      _connection_pool->release(conn);
    }
  }

  void handle_write_event(UringConnectionInfo *conn, int result) {
    if (result > 0) {
      conn->write_buffer.read_data(result);
      set_read_event(conn);
    } else {
      std::cerr << "Write失败: " << result << std::endl;
      _connection_pool->release(conn);
    }
  }

  void handle_close_event(UringConnectionInfo *conn) {
    _connection_pool->release(conn);
  }

public:
  IoUringServer(int port = TCP_DEFAULT_PORT)
      : _tcp_listener(port), _running(false) {

    if (!initialize_uring()) {
      throw std::runtime_error("io_uring初始化失败");
    }

    if (!initialize_thread_pool()) {
      std::cerr << "线程池初始化失败，将使用单线程模式" << std::endl;
    }
  }

  void run() {
    int listen_fd = _tcp_listener.get_listen_fd();
    if (listen_fd < 0) {
      throw std::runtime_error("获取监听套接字失败");
    }

    // 预先创建accept事件
    for (size_t i = 0; i < 10; ++i) {
      if (!set_accept_event(listen_fd)) {
        std::cerr << "设置accept事件失败" << std::endl;
      }
    }

    _running = true;
    io_uring_submit(&_ring);

    while (_running) {
      struct io_uring_cqe *cqe;
      int ret = io_uring_peek_cqe(&_ring, &cqe);

      if (ret == 0) {
        process_completion_events();
        io_uring_submit(&_ring);
      } else if (ret == -EAGAIN) {
        // 没有完成事件，短暂等待
        usleep(1000);
      } else {
        std::cerr << "检查完成队列失败: " << ret << std::endl;
        break;
      }
    }
  }

  void stop() { _running = false; }

  ~IoUringServer() {
    stop();
    io_uring_queue_exit(&_ring);
  }

  // 禁用拷贝构造和赋值
  IoUringServer(const IoUringServer &) = delete;
  IoUringServer &operator=(const IoUringServer &) = delete;
};