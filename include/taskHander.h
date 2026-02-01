#pragma once
#include "http_complete.h"
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

// 简洁的任务处理器接口 - 模板化设计
template <typename ContextType> class TaskHandler {
public:
  virtual ~TaskHandler() = default;

  // 检查是否可以处理该上下文
  virtual bool can_handle(ContextType *context) = 0;
  //报文完整否
  virtual bool is_parse_complete(ContextType *context) = 0;
  // 处理任务
  virtual void handle(ContextType *context) = 0;

  // 获取处理器名称
  virtual TaskType get_name() const = 0;
};

// 默认的HTTP任务处理器
template <typename ContextType>
class DefaultHttpHandler : public TaskHandler<ContextType> {
public:
  bool can_handle(ContextType *context) override {
    // 检查是否为HTTP任务
    return HttpTask::is_http_task(context);
  }
  bool is_parse_complete(ContextType *context) override {
    return HttpTask::is_http_parse_complete(context);
  }

  void handle(ContextType *context) override {
    HttpTask task;
    task.handle_message(context);
  }

  TaskType get_name() const override { return TaskType::HTTP; }
};

// 默认的文件处理器
template <typename ContextType>
class DefaultFileHandler : public TaskHandler<ContextType> {
public:
  bool can_handle(ContextType *context) override {
    // 简单的文件传输协议检测
    size_t readable = context->read_buffer.get_readable_size();
    if (readable < 8)
      return false;

    char *data = context->read_buffer.get_read_head();
    // 检测文件传输协议标识
    return (data[0] == 'F' && data[1] == 'I' && data[2] == 'L' &&
            data[3] == 'E');
  }

  bool is_parse_complete(ContextType *context) override {
    // 简单的文件协议完整性检查
    size_t readable = context->read_buffer.get_readable_size();
    if (readable < 12)
      return false; // 需要至少12字节的协议头

    char *data = context->read_buffer.get_read_head();
    // 检查文件协议结束标记
    return (data[readable - 1] == '\n' && data[readable - 2] == '\r');
  }

  void handle(ContextType *context) override {
    std::cout << "处理文件传输请求" << std::endl;
  }

  TaskType get_name() const override { return TaskType::FILE; }
};

// 默认的聊天室处理器
template <typename ContextType>
class DefaultChatHandler : public TaskHandler<ContextType> {
public:
  bool can_handle(ContextType *context) override {
    // 简单的聊天室协议检测
    size_t readable = context->read_buffer.get_readable_size();
    if (readable < 6)
      return false;

    char *data = context->read_buffer.get_read_head();
    // 检测聊天室协议标识
    return (data[0] == 'C' && data[1] == 'H' && data[2] == 'A' &&
            data[3] == 'T' && data[4] == ':' && data[5] == ' ');
  }

  bool is_parse_complete(ContextType *context) override {
    // 简单的聊天协议完整性检查
    size_t readable = context->read_buffer.get_readable_size();
    if (readable < 1)
      return false;

    char *data = context->read_buffer.get_read_head();
    // 检查消息结束符
    for (size_t i = 0; i < readable; ++i) {
      if (data[i] == '\n')
        return true;
    }
    return false;
  }

  void handle(ContextType *context) override {
    std::cout << "处理聊天室请求" << std::endl;
  }

  TaskType get_name() const override { return TaskType::CHAT; }
};