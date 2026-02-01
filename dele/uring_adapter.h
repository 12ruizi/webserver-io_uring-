#pragma once
#include "../lib/http/http_complete.h"
#include "factory/dispatcher.h"
#include "factory/taskHander.h"
#include "pthread_pool.h"
#include "uring_types.h"
#include <functional>
#include <iostream>
// UringConnectionInfo专用的适配器实现
class UringTaskAdapter {
private:
  // 具体的HTTP处理器实现
  class UringHttpHandler : public TaskHandler<UringConnectionInfo> {
  private:
    ThreadPool *_thread_pool;

  public:
    UringHttpHandler(ThreadPool *thread_pool = nullptr)
        : _thread_pool(thread_pool) {}

    bool can_handle(UringConnectionInfo *info) override {
      // 判断是否是HTTP请求
      return HttpTask::is_http_task(info);
    }

    void handle(UringConnectionInfo *info) override {
      // 判断报文完整性
      size_t parse_result = HttpTask::is_http_parse_complete(info);

      if (parse_result == 0) {
        // 报文不完整，需要更多数据
        std::cout << "HTTP报文不完整，需要更多数据，fd=" << info->fd
                  << std::endl;
        info->parse_result = ParseResult::NEEED_MORE_DATA;
        // 这里不直接设置读任务，由主线程处理
      } else {
        // 报文完整，提交给线程池处理
        std::cout << "HTTP报文完整，提交线程池处理，fd=" << info->fd
                  << std::endl;
        info->parse_result = ParseResult::COMPLETE;

        if (_thread_pool) {
          // 创建任务并提交到线程池
          auto task = [info, this]() {
            // 线程池中处理HTTP请求
            HttpTask::handle_http_request(info);

            // 处理完成后，通过回调通知主线程设置写任务
            if (_thread_pool) {
              _thread_pool->notify_main_thread(
                  info,
                  [](UringConnectionInfo *processed_conn) {
                    // 主线程回调：设置写事件
                    std::cout << "线程池处理完成，设置写事件，fd="
                              << processed_conn->fd << std::endl;
                    // 这里需要主线程调用set_write_event
                  },
                  TaskPriority::HIGH);
            }
          };

          _thread_pool->enqueue(task);
        } else {
          // 如果没有线程池，直接处理
          HttpTask::handle_http_request(info);
        }
      }
    }

    const char *get_name() const override { return "HTTP"; }

    // 设置线程池
    void set_thread_pool(ThreadPool *thread_pool) {
      _thread_pool = thread_pool;
    }
  };

  // 具体的文件处理器实现
  class UringFileHandler : public TaskHandler<UringConnectionInfo> {
  public:
    bool can_handle(UringConnectionInfo *info) override {
      // 实现文件传输协议检测
      size_t readable = info->read_buffer.get_readable_size();
      if (readable < 8)
        return false;

      char *data = info->read_buffer.get_read_head();
      return (data[0] == 'F' && data[1] == 'I' && data[2] == 'L' &&
              data[3] == 'E');
    }

    void handle(UringConnectionInfo *info) override {
      std::cout << "处理Uring文件传输请求" << std::endl;
      // 实现具体的文件传输逻辑
    }

    const char *get_name() const override { return "FILE"; }
  };

  // 具体的聊天室处理器实现
  class UringChatHandler : public TaskHandler<UringConnectionInfo> {
  public:
    bool can_handle(UringConnectionInfo *info) override {
      // 实现聊天室协议检测
      size_t readable = info->read_buffer.get_readable_size();
      if (readable < 6)
        return false;

      char *data = info->read_buffer.get_read_head();
      return (data[0] == 'C' && data[1] == 'H' && data[2] == 'A' &&
              data[3] == 'T' && data[4] == ':' && data[5] == ' ');
    }

    void handle(UringConnectionInfo *info) override {
      std::cout << "处理Uring聊天室请求" << std::endl;
      // 实现具体的聊天室逻辑
    }

    const char *get_name() const override { return "CHAT"; }
  };

public:
  // 创建预配置的Uring任务分发器（支持线程池）
  static TaskDispatcher<UringConnectionInfo>
  create_uring_dispatcher(ThreadPool *thread_pool = nullptr) {
    TaskDispatcher<UringConnectionInfo> dispatcher;

    auto http_handler = std::make_unique<UringHttpHandler>(thread_pool);
    dispatcher.register_handler(std::move(http_handler));
    dispatcher.register_handler(std::make_unique<UringFileHandler>());
    dispatcher.register_handler(std::make_unique<UringChatHandler>());

    return dispatcher;
  }

  // 便捷的静态方法（支持线程池）
  static bool process_uring_task(UringConnectionInfo *info,
                                 ThreadPool *thread_pool = nullptr) {
    static auto dispatcher = create_uring_dispatcher(thread_pool);
    return dispatcher.dispatch(info);
  }

  // 获取特定类型的处理器
  template <typename HandlerType>
  static std::unique_ptr<TaskHandler<UringConnectionInfo>> create_handler() {
    static_assert(
        std::is_base_of_v<TaskHandler<UringConnectionInfo>, HandlerType>,
        "HandlerType must be a UringConnectionInfo handler");
    return std::make_unique<HandlerType>();
  }
};