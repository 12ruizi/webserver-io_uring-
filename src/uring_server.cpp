#include "../include/uring_server.h"
#include <csignal>
#include <cstring>
#include <thread>

// io_uring服务器类实现
IoUringServer::IoUringServer(int port)
    : _ring(nullptr), _tcp_listener(std::make_unique<TcpListener>(port)),
      _memory_pool(std::make_unique<LayerMemoryPool>()), _running(false),
      _main_queue(std::make_shared<MainThreadTaskQueue>()),
      _thread_pool(std::make_unique<ThreadPool>()) {

  if (!initialize_uring()) {
    throw std::runtime_error("初始化io_uring失败");
  }

  // 设置线程池的主线程队列
  _thread_pool->set_main_queue(_main_queue);

  _task_dispatcher = std::make_shared<TaskDispatcher>(_thread_pool, _main_queue,
                                                      _ring, _memory_pool);
}

IoUringServer::~IoUringServer() {
  stop();
  if (_ring) {
    io_uring_queue_exit(_ring.get());
  }
}

bool IoUringServer::initialize_uring() {
  // 使用make_shared正确创建shared_ptr
  auto ring_ptr = std::make_shared<io_uring>();
  struct io_uring_params params;
  memset(&params, 0, sizeof(params));

  int ret =
      io_uring_queue_init_params(URING_MAX_QUEUE, ring_ptr.get(), &params);
  if (ret < 0) {
    std::cerr << "io_uring初始化失败: " << strerror(-ret) << std::endl;
    return false;
  }

  _ring = ring_ptr; // 正确赋值
  return true;
}

bool IoUringServer::set_accept_event(int listen_fd) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(_ring.get());
  if (!sqe) {
    return false;
  }

  UringConnectionInfo *conn = _memory_pool->acquire_connection();
  if (!conn) {
    return false;
  }

  conn->fd = -1; // 等待accept分配
  conn->state = UringConnectionState::ACCEPT;

  io_uring_prep_accept(sqe, listen_fd, (struct sockaddr *)&conn->addr,
                       &conn->addrlen, 0);
  io_uring_sqe_set_data(sqe, conn);

  return true;
}

bool IoUringServer::set_read_event(UringConnectionInfo *conn) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(_ring.get());
  if (!sqe) {
    // 如果无法获取sqe，先提交当前队列
    io_uring_submit(_ring.get());
    sqe = io_uring_get_sqe(_ring.get());
    if (!sqe) {
      return false;
    }
  }

  conn->state = UringConnectionState::READ;
  char *buffer = conn->read_buffer.get_write_tail();
  size_t size = conn->read_buffer.get_writable_size();

  io_uring_prep_read(sqe, conn->fd, buffer, size, 0);
  io_uring_sqe_set_data(sqe, conn);
  std::cout << "设置读事件: fd=" << conn->fd << ", size=" << size << std::endl;

  // 立即提交事件
  int submit_ret = io_uring_submit(_ring.get());
  if (submit_ret < 0) {
    std::cerr << "提交读事件失败: " << submit_ret << std::endl;
    return false;
  }
  return true;
}

bool IoUringServer::set_write_event(UringConnectionInfo *conn) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(_ring.get());
  if (!sqe) {
    // 如果无法获取sqe，先提交当前队列
    io_uring_submit(_ring.get());
    sqe = io_uring_get_sqe(_ring.get());
    if (!sqe) {
      return false;
    }
  }

  conn->state = UringConnectionState::WRITE;
  char *buffer = conn->write_buffer.get_read_head();
  size_t size = conn->write_buffer.get_readable_size();

  io_uring_prep_write(sqe, conn->fd, buffer, size, 0);
  io_uring_sqe_set_data(sqe, conn);
  std::cout << "设置写事件: fd=" << conn->fd << ", size=" << size << std::endl;

  // 立即提交事件
  int submit_ret = io_uring_submit(_ring.get());
  if (submit_ret < 0) {
    std::cerr << "提交写事件失败: " << submit_ret << std::endl;
    return false;
  }
  return true;
}

bool IoUringServer::set_close_event(UringConnectionInfo *conn) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(_ring.get());
  if (!sqe) {
    return false;
  }

  conn->state = UringConnectionState::CLOSE;
  io_uring_prep_close(sqe, conn->fd);
  io_uring_sqe_set_data(sqe, conn);

  return true;
}

// 在handle_accept_event中设置主线程队列
void IoUringServer::handle_accept_event(UringConnectionInfo *conn, int result) {
  if (result >= 0) {
    conn->fd = result;
    std::cout << "新连接接受: fd=" << conn->fd << std::endl;

    // 设置主线程队列引用
    conn->set_main_queue(_main_queue);

    // 设置非阻塞模式
    int flags = fcntl(conn->fd, F_GETFL, 0);
    fcntl(conn->fd, F_SETFL, flags | O_NONBLOCK);

    // 开始读取数据
    set_read_event(conn);

    // 为下一个连接准备accept
    int listen_fd = _tcp_listener->get_listen_fd();
    set_accept_event(listen_fd);
  } else {
    std::cerr << "Accept失败: " << result << std::endl;
    _memory_pool->release_connection(conn);
  }
}

void IoUringServer::process_completion_events() {
  struct io_uring_cqe *cqe;
  unsigned head;
  unsigned count = 0;

  io_uring_for_each_cqe(_ring.get(), head, cqe) {
    count++;
    UringConnectionInfo *conn =
        static_cast<UringConnectionInfo *>(io_uring_cqe_get_data(cqe));

    if (conn) {
      handle_completion_event(conn, cqe->res);
    }

    io_uring_cqe_seen(_ring.get(), cqe);
  }
}

void IoUringServer::handle_completion_event(UringConnectionInfo *conn,
                                            int result) {
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
    std::cerr << "未知连接状态: " << static_cast<int>(conn->state) << std::endl;
    _memory_pool->release_connection(conn);
    break;
  }
}

void IoUringServer::handle_close_event(UringConnectionInfo *conn) {
  std::cout << "连接关闭完成: fd=" << conn->fd << std::endl;
  _memory_pool->release_connection(conn);
}

void IoUringServer::handle_read_event(UringConnectionInfo *conn,
                                      size_t result) {
  if (result == 0) {
    // 连接关闭
    std::cout << "handle_read---------------连接关闭: fd=" << conn->fd
              << std::endl;
    set_close_event(conn);
  } else if (result > 0) {
    // 读取数据成功，判断是读到哪里的数据
    if (conn->extra_buffer != nullptr) {
      // 判断大小是否等于info->bytes_NO_read
      if (result == conn->bytes_NO_read) {
        conn->parse_result = ParseResult::COMPLETE;
        // 使用任务分发器处理
        bool handled = _task_dispatcher->dispatch(conn);
        if (!handled) {
          std::cout << "没有合适的处理器，继续读取，fd=" << conn->fd
                    << std::endl;
          set_read_event(conn);
        }
      } else {
        // 数据不完整，继续读取到extra_buffer
        std::cout << "数据不完整，继续读取，fd=" << conn->fd
                  << "，已读取=" << result << "，期望=" << conn->bytes_NO_read
                  << std::endl;
        set_read_event(conn);
      }
    } else {
      conn->read_buffer.write_data(result); // 读取数据成功
      std::cout << "读取数据: " << result << "字节, fd=" << conn->fd
                << std::endl;

      // 使用任务分发器处理
      bool handled = _task_dispatcher->dispatch(conn);

      if (!handled) {
        std::cout << "没有合适的处理器，继续读取，fd=" << conn->fd << std::endl;
        set_read_event(conn);
      }
    }
  } else {
    perror("read");
    _memory_pool->release_connection(conn);
  }
}

void IoUringServer::handle_write_event(UringConnectionInfo *conn, int result) {
  if (result > 0) {
    conn->write_buffer.read_data(result);
    std::cout << "写入数据: " << result << "字节, fd=" << conn->fd << std::endl;

    // 检查是否还有数据需要写入
    if (conn->write_buffer.get_readable_size() > 0) {
      // 还有数据，继续写入
      set_write_event(conn);
    } else {
      // 数据写入完成，继续读取下一个请求
      set_read_event(conn);
    }
  } else {
    std::cerr << "Write失败: " << result << ", fd=" << conn->fd << std::endl;
    _memory_pool->release_connection(conn);
  }
}

void IoUringServer::process_main_thread_tasks() {
  // 非阻塞处理主线程任务队列
  MainThreadTask task(nullptr, nullptr);
  while (_main_queue->try_pop_task(task)) {
    if (task.conn && task.callback) {
      std::cout << "主线程处理回调任务，fd=" << task.conn->fd << std::endl;
      task.callback(task.conn);
    }
  }
}

void IoUringServer::run() {
  int listen_fd = _tcp_listener->get_listen_fd();
  if (listen_fd < 0) {
    throw std::runtime_error("获取监听套接字失败");
  }

  // 预先创建accept事件
  for (size_t i = 0; i < 10; ++i) {
    if (!set_accept_event(listen_fd)) {
      std::cerr << "设置accept事件失败" << std::endl;
    }
  }

  // 注册处理器
  _task_dispatcher->register_handler(
      std::make_unique<DefaultHttpHandler<UringConnectionInfo>>());
  _task_dispatcher->register_handler(
      std::make_unique<DefaultFileHandler<UringConnectionInfo>>());
  _running = true;
  io_uring_submit(_ring.get());

  std::cout << "服务器开始运行，监听端口: " << TCP_DEFAULT_PORT << std::endl;

  while (_running) {
    struct io_uring_cqe *cqe;
    int ret = io_uring_wait_cqe(_ring.get(), &cqe);

    if (ret == 0) {
      process_completion_events();
      process_main_thread_tasks(); // 处理线程池回调

      // 关键修复：每次处理完事件后都提交，确保及时性
      int submit_ret = io_uring_submit(_ring.get());
      if (submit_ret < 0) {
        std::cerr << "提交io_uring事件失败: " << submit_ret << std::endl;
      }
    } else if (ret == -EAGAIN) {
      // 没有完成事件，处理主线程任务
      process_main_thread_tasks();

      // 即使没有完成事件，也尝试提交新事件
      int submit_ret = io_uring_submit(_ring.get());
      if (submit_ret < 0 && submit_ret != -EBUSY) {
        std::cerr << "提交io_uring事件失败: " << submit_ret << std::endl;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } else {
      std::cerr << "检查完成队列失败: " << ret << std::endl;
      break;
    }
  }
}

void IoUringServer::stop() {
  if (_running) {
    _running = false;
    std::cout << "停止服务器..." << std::endl;
  }
}