#include "funC.h"
#pragma once
#include "http_complete.h"
#include <cstdio>
// worker类 负责处理具体的工作  ，肯定要解析报文处理是哪一种任务（分类 ）
class TaskHandler {
public:
  virtual ~TaskHandler() = default;
  virtual bool can_handle(Info_net_s *info) = 0;
  virtual void handle(Info_net_s *info) = 0;
  virtual const char *get_name() const = 0; // 添加const限定符
};

class HttpTaskHandler : public TaskHandler { // 修正类名拼写
public:
  bool can_handle(Info_net_s *info) override {
    // 使用静态方法检查HTTP报文完整性
    return HttpTask::is_http_parse_complete(info) > 0;
  }

  void handle(Info_net_s *info) override {
    // 直接调用静态方法，无需创建对象
    HttpTask::handle_http_request(info);
  }

  const char *get_name() const override { return "HTTP"; }
};

// 聊天室任务处理器
class ChatTaskHandler : public TaskHandler {
public:
  bool can_handle(Info_net_s *info) override {
    // 检查是否是聊天室协议
    // 实现聊天室协议检测逻辑
    return false; // 待实现
  }

  void handle(Info_net_s *info) override {
    // 实现聊天室处理逻辑
  }

  const char *get_name() const override { return "CHAT"; }
};

// 文件传输任务处理器
class FileTransferHandler : public TaskHandler {
public:
  bool can_handle(Info_net_s *info) override {
    // 检查是否是文件传输协议
    return false; // 待实现
  }

  void handle(Info_net_s *info) override {
    // 实现大文件传输逻辑
  }

  const char *get_name() const override { return "FILE"; }
};