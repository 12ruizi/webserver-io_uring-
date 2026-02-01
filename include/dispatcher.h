#pragma once
#include "memery_pool.h"
#include "pthread_pool.h"
#include "taskHander.h"
#include "uring_types.h"
#include <iostream>
#include <liburing.h>
#include <memory>

// 简洁的任务分发器
class TaskDispatcher {
private:
  std::vector<std::unique_ptr<TaskHandler<UringConnectionInfo>>> handlers_;
  std::shared_ptr<ThreadPool> _pool;
  std::shared_ptr<MainThreadTaskQueue> _queue;
  std::shared_ptr<io_uring> _uring;
  std::shared_ptr<LayerMemoryPool> _memory_pool;

public:
  TaskDispatcher(std::shared_ptr<ThreadPool> pool = nullptr,
                 std::shared_ptr<MainThreadTaskQueue> queue = nullptr,
                 std::shared_ptr<io_uring> uring = nullptr,
                 std::shared_ptr<LayerMemoryPool> memory_pool = nullptr)
      : _pool(pool), _queue(queue), _uring(uring), _memory_pool(memory_pool){};

  ~TaskDispatcher() = default;

  // 注册处理器实例
  void
  register_handler(std::unique_ptr<TaskHandler<UringConnectionInfo>> handler) {
    handlers_.push_back(std::move(handler));
  }

  // 分发任务
  bool dispatch(UringConnectionInfo *context) {

    for (auto &handler : handlers_) {
      if (handler->can_handle(context)) {
        std::cout << "可以处理" << std::endl;
        context->task_type = handler->get_name();
        if (handler->is_parse_complete(context)) {
          context->parse_result = ParseResult::COMPLETE;
          if (_pool) {
            // 将任务提交到线程池，并设置回调
            auto handler_ptr = handler.get();

            // 定义回调函数：线程池完成任务后立即设置并提交写事件
            auto callback = [this](UringConnectionInfo *processed_conn) {
              std::cout << "线程池处理完成，fd=" << processed_conn->fd
                        << "，写缓冲区大小="
                        << processed_conn->write_buffer.get_readable_size()
                        << std::endl;

              // 立即设置并提交写事件，避免等待下一个事件循环
              if (_uring) {
                struct io_uring_sqe *sqe = io_uring_get_sqe(_uring.get());
                if (sqe) {
                  char *buffer = processed_conn->write_buffer.get_read_head();
                  size_t size =
                      processed_conn->write_buffer.get_readable_size();
                  if (size > 0) {
                    io_uring_prep_write(sqe, processed_conn->fd, buffer, size,
                                        0);
                    io_uring_sqe_set_data(sqe, processed_conn);
                    processed_conn->state = UringConnectionState::WRITE;

                    // 关键修复：立即提交io_uring事件
                    int submit_ret = io_uring_submit(_uring.get());
                    std::cout << "立即提交写事件，fd=" << processed_conn->fd
                              << "，数据大小=" << size
                              << "字节，提交结果=" << submit_ret << std::endl;

                    // 打印响应内容的前100个字符用于调试
                    std::string response_preview(buffer,
                                                 std::min(size, size_t(100)));
                    std::cout << "响应预览: " << response_preview << std::endl;
                  } else {
                    // 写缓冲区为空，设置读事件继续处理
                    buffer = processed_conn->read_buffer.get_write_tail();
                    size = processed_conn->read_buffer.get_writable_size();
                    io_uring_prep_read(sqe, processed_conn->fd, buffer, size,
                                       0);
                    io_uring_sqe_set_data(sqe, processed_conn);
                    processed_conn->state = UringConnectionState::READ;

                    // 立即提交读事件
                    int submit_ret = io_uring_submit(_uring.get());
                    std::cout << "写缓冲区为空，立即提交读事件，fd="
                              << processed_conn->fd
                              << "，提交结果=" << submit_ret << std::endl;
                  }
                } else {
                  std::cout << "无法获取sqe，尝试提交当前队列后重新获取"
                            << std::endl;
                  // 提交当前队列，然后重新获取sqe
                  io_uring_submit(_uring.get());
                  sqe = io_uring_get_sqe(_uring.get());
                  if (sqe) {
                    char *buffer = processed_conn->write_buffer.get_read_head();
                    size_t size =
                        processed_conn->write_buffer.get_readable_size();
                    if (size > 0) {
                      io_uring_prep_write(sqe, processed_conn->fd, buffer, size,
                                          0);
                      io_uring_sqe_set_data(sqe, processed_conn);
                      processed_conn->state = UringConnectionState::WRITE;

                      // 立即提交
                      int submit_ret = io_uring_submit(_uring.get());
                      std::cout << "重新获取sqe后立即提交写事件，fd="
                                << processed_conn->fd
                                << "，提交结果=" << submit_ret << std::endl;
                    }
                  }
                }
              } else {
                std::cout << "uring为空，无法设置写事件" << std::endl;
              }
            };
            // 使用带回调的任务提交，线程池完成函数后执行回调函数；
            _pool->enqueue_with_callback(
                [handler_ptr, context]() { handler_ptr->handle(context); },
                context, callback);
            std::cout << "线程(任务+回调任务)提交完成,(但是任务可能未完成)fd="
                      << context->fd << "--------------------------dispatcher"
                      << std::endl;
          } else {
            // 如果没有线程池，直接在当前线程处理
            handler->handle(context);
          }
          return true;
        }
        // 报文不完整，需要继续读取数据
        else {
          if (_queue && _uring) {
            // 创建读数据的任务
            bool ret = _queue->push_task(
                context, [context, this](UringConnectionInfo *ctx) {
                  // 设置io_uring的读任务
                  ctx = context;
                  struct io_uring_sqe *sqe = io_uring_get_sqe(_uring.get());
                  if (sqe) {
                    //从内存池获得空间
                    std::cout << "----dispatcher 报文不完整，需要继续读取数据"
                              << ctx->bytes_NO_read << std::endl;
                    ctx->extra_buffer =
                        _memory_pool->allocate_buffer(ctx->bytes_NO_read);
                    ctx->extra_buffer_in_use = true;
                    io_uring_prep_read(sqe, ctx->fd, ctx->extra_buffer,
                                       ctx->bytes_NO_read, 0);
                    io_uring_sqe_set_data(sqe, ctx);
                  }
                });

            if (ret) {
              return true; // 返回true表示已处理（创建了读任务）
            } else {
              std::cerr << "任务队列已满，无法添加读任务" << std::endl;
              return false;
            }
          } else {
            std::cerr << "缺少队列或uring实例，无法创建读任务" << std::endl;
            return false;
          }
        }
      }
    }
    // 没有找到合适的处理器，也没有更新rbuffer的write_tail，所以这里可能直接放弃，有可能造成数据丢失，接下来的信息可能解析错误！
    //正确办法应该继续找，把整个完整的报文解析出来，然后再处理
    //然后这里应该增加一个taskhandler，不应该是没找到合适处理，所有服务端不能解析的请求工作都有这个taskhandler处理！
    std::cout << "没有找到合适的任务处理器1" << std::endl;
    return false;
  }

  // 获取处理器数量
  size_t size() const { return handlers_.size(); }

  // 清空所有处理器
  void clear() { handlers_.clear(); }
};