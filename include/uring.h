#include "dispatcher.h"
#include "funC.h"
#include "pthread_pool.h" // 使用C++线程池
#include "taskHander.h"
#include "tcp.h"
#if 1
class _io_uring {
private:
  Tcp_listen _tcp_socket;
  std::vector<std::shared_ptr<Info_net_s>> connections;
  std::mutex connections_mutex;
  struct io_uring _ring;
  std::unique_ptr<ThreadPool> thread_pool;
  // 连接处理函数（用于线程池）
  void process_request(Info_net_s *conn) {
    if (!conn) {
      return;
    }
    //使用任务分发器处理
    TaskDispatcher::process_task(conn); // 统一使用TaskDispatcher
    // 设置写事件（需要在主线程中设置）
    set_event_write(conn);
  }
  std::shared_ptr<Info_net_s> find_shared_connection(Info_net_s *conn) {
    std::lock_guard<std::mutex> lock(connections_mutex);
    auto it = std::find_if(connections.begin(), connections.end(),
                           [conn](const std::shared_ptr<Info_net_s> &c) {
                             return c.get() == conn;
                           });
    return (it != connections.end()) ? *it : std::shared_ptr<Info_net_s>();
  }

  bool init() {
    int ret = io_uring_queue_init(MAX_Queue, &_ring, 0);
    return ret == 0;
  }

  // 初始化线程池
  bool init_thread_pool(size_t thread_count = 4) {
    try {
      thread_pool = std::make_unique<ThreadPool>(thread_count);
      return true;
    } catch (const std::exception &e) {
      std::cerr << "线程池初始化失败: " << e.what() << std::endl;
      return false;
    }
  }

  bool set_event_accept(int listen_Fd, std::shared_ptr<Info_net_s> conn) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&_ring);
    if (!sqe) {
      perror("Failed to get SQE");
      return false;
    }
    conn->fd = -1;
    conn->state = CONN_ACCEPT;
    io_uring_prep_accept(sqe, listen_Fd, (struct sockaddr *)&conn->addr,
                         &conn->addrlen, 0);
    io_uring_sqe_set_data(sqe, conn.get());
    return true;
  }

  bool set_event_read(Info_net_s *conn) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&_ring);
    if (!sqe) {
      fprintf(stderr, "Failed to get SQE\n");
      return false;
    }
    conn->state = CONN_READ;
    char *buffer = conn->rubffer.get_write_tail();
    size_t buffer_size = conn->rubffer.get_writable_size();
    if (buffer_size == 0) {
      // 缓冲区满时清空并重新设置读取
      conn->rubffer.clear();
      buffer = conn->rubffer.get_write_tail();
      buffer_size = conn->rubffer.get_writable_size();
    }
    io_uring_prep_read(sqe, conn->fd, buffer, buffer_size, 0);
    io_uring_sqe_set_data(sqe, conn);
    return true;
  }

  bool set_event_write(Info_net_s *conn) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&_ring);
    if (!sqe) {
      perror("Failed to get SQE");
      return false;
    }
    conn->state = CONN_WRITE;
    io_uring_prep_write(sqe, conn->fd, conn->wbuffer.get_read_head(),
                        conn->wbuffer.get_readable_size(), 0);
    io_uring_sqe_set_data(sqe, conn);
    return true;
  }

  bool set_event_close(std::shared_ptr<Info_net_s> conn) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&_ring);
    if (!sqe) {
      perror("Failed to get SQE");
      return false;
    }
    conn->state = CONN_CLOSE;
    io_uring_prep_close(sqe, conn->fd);
    io_uring_sqe_set_data(sqe, conn.get());

    // 从连接列表中移除
    {
      std::lock_guard<std::mutex> lock(connections_mutex);
      auto it = std::find_if(connections.begin(), connections.end(),
                             [conn](const std::shared_ptr<Info_net_s> &c) {
                               return c.get() == conn.get();
                             });
      if (it != connections.end()) {
        connections.erase(it);
      }
    }

    return true;
  }

  // 非阻塞获取所有完成事件
  void process_all_completions() {
    struct io_uring_cqe *cqe;
    unsigned head;
    unsigned count = 0;
    // 非阻塞获取所有完成事件（这是一个宏）或者使用io_uring_peek_bath_cqe(&ring,cqes,Batch_size);
    io_uring_for_each_cqe(&_ring, head, cqe) {
      count++;
      Info_net_s *conn = (Info_net_s *)io_uring_cqe_get_data(cqe);
      if (!conn) {
        continue;
      }
      int ret = cqe->res;
      std::shared_ptr<Info_net_s> shared_conn = find_shared_connection(conn);
      if (ret < 0) {
        std::cerr << "I/O 操作失败: " << strerror(-ret) << std::endl;
        // 查找对应的智能指针
        if (shared_conn) {
          set_event_close(shared_conn);
        }
        continue;
      }
      if (!shared_conn) {
        continue;
      }
      switch (conn->state) {
      case CONN_ACCEPT: {
        conn->state = CONN_READ;
        conn->fd = ret;
        set_event_read(conn);
        // 添加新连接
        {
          std::lock_guard<std::mutex> lock(connections_mutex);
          connections.push_back(shared_conn);
        }
        break;
      }
      case CONN_READ: {
        if (ret == 0) {
          set_event_close(shared_conn);
        } else {
          conn->rubffer.write_date(ret);
          // 将任务提交到C++线程池处理
          if (thread_pool) {
            // 使用lambda捕获this指针和conn指针
            thread_pool->enqueue([this, shared_conn]() {
              TaskDispatcher::process_task(
                  shared_conn.get()); // 统一使用TaskDispatcher
              this->set_event_write(shared_conn.get());
            });
          } else {
            // 如果没有线程池，直接处理
            TaskDispatcher::process_task(
                shared_conn.get()); // 统一使用TaskDispatcher
            this->set_event_write(shared_conn.get());
          }
        }
        break;
      }
      case CONN_WRITE: {
        conn->wbuffer.read_date(ret);
        set_event_read(shared_conn.get());
        break;
      }
      default: {
        fprintf(stderr, "Unknown connection state\n");
        break;
      }
      }
    }

    // 标记所有完成事件已处理
    io_uring_cq_advance(&_ring, count);
  }

public:
  _io_uring() {
    if (!init()) {
      fprintf(stderr, "io_uring 初始化失败\n");
    }
    // 初始化C++线程池
    if (!init_thread_pool()) {
      fprintf(stderr, "线程池初始化失败，将使用单线程模式\n");
    }
  }

  void event_Loop() {
    int listen_fd = _tcp_socket.get_listenFd();
    if (listen_fd < 0) {
      fprintf(stderr, "获取监听套接字失败\n");
      return;
    }

    // 预先创建一些连接
    for (size_t i = 0; i < 20; ++i) {
      auto conn = std::make_shared<Info_net_s>();
      if (!set_event_accept(listen_fd, conn)) {
        fprintf(stderr, "设置accept事件失败\n");
        continue;
      }
      {
        std::lock_guard<std::mutex> lock(connections_mutex);
        connections.push_back(conn);
      }
    }

    io_uring_submit(&_ring);

    while (true) {
      // 非阻塞等待事件
      struct io_uring_cqe *cqe;
      int ret = io_uring_peek_cqe(&_ring, &cqe);
      if (ret == 0) {
        // 有完成事件，处理所有事件
        process_all_completions();
        io_uring_submit(&_ring);
      } else if (ret == -EAGAIN) {
        // 没有完成事件，短暂等待
        usleep(1000); // 1ms
      } else {
        fprintf(stderr, "检查完成队列失败\n");
        break;
      }
    }
  }

  ~_io_uring() {
    // 线程池会自动在析构时关闭
    // 清理连接
    {
      std::lock_guard<std::mutex> lock(connections_mutex);
      connections.clear();
    }

    io_uring_queue_exit(&_ring);
  }
};

#else

#endif