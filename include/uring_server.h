#pragma once
// C系统头文件
#include <liburing.h>
#include <liburing/io_uring.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

// C++标准库头文件
#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>

// 项目头文件
#include "dispatcher.h"
#include "memery_pool.h"
#include "pthread_pool.h"
#include "taskHander.h"
#include "tcp.h"
#include "uring_types.h"

// io_uring模块专用配置
// io_uring服务器主类声明
//可以设置一个定时器，定时检查连接的活跃状态，关闭长时间不活动的连接
//定时器，定时释放内存池中过期的缓冲区（如何知道他是否过期？
//缓冲区可以有一个时间戳，记录最后一次使用的时间，每次使用时更新这个时间戳，
//定时器检查当前时间与时间戳的差值，超过一定阈值则认为过期）//万一没有过期呢？
//
class IoUringServer {
private:
  // 私有方法声明
  bool initialize_uring();
  bool set_accept_event(int listen_fd);
  bool set_read_event(UringConnectionInfo *conn);
  bool set_write_event(UringConnectionInfo *conn);
  bool set_close_event(UringConnectionInfo *conn);
  void process_completion_events();
  void handle_completion_event(UringConnectionInfo *conn, int result);
  void handle_accept_event(UringConnectionInfo *conn, int result);
  void handle_read_event(UringConnectionInfo *conn, size_t result);
  void handle_write_event(UringConnectionInfo *conn, int result);
  void handle_close_event(UringConnectionInfo *conn);
  void process_main_thread_tasks();
  std::shared_ptr<io_uring> _ring;
  std::unique_ptr<TcpListener> _tcp_listener;
  std::shared_ptr<LayerMemoryPool> _memory_pool;
  std::atomic<bool> _running;

  // 主线程任务队列和线程池
  std::shared_ptr<MainThreadTaskQueue> _main_queue;
  std::shared_ptr<ThreadPool> _thread_pool;
  std::shared_ptr<TaskDispatcher> _task_dispatcher;

public:
  // 构造函数和析构函数声明
  IoUringServer(int port = TCP_DEFAULT_PORT);
  ~IoUringServer();

  // 公共方法声明
  void run();
  void stop();

  // 禁用拷贝构造和赋值
  IoUringServer(const IoUringServer &) = delete;
  IoUringServer &operator=(const IoUringServer &) = delete;
};