#pragma once
#include "funC.h"
#include "taskHander.h" // 修正文件名拼写
#include <iostream>
#include <memory>
#include <vector>

class TaskDispatcher { // 修正类名拼写
private:
  std::vector<std::unique_ptr<TaskHandler>> _handlers; // 修正变量名

public:
  TaskDispatcher() { // 修正构造函数名
    //注册所有任务处理器
    _handlers.push_back(std::make_unique<HttpTaskHandler>()); // 修正类名
    _handlers.push_back(std::make_unique<ChatTaskHandler>());
    _handlers.push_back(std::make_unique<FileTransferHandler>());
  }

  //分发任务到合适的处理器
  bool dispatch(Info_net_s *info) {
    for (auto &handler : _handlers) {
      if (handler->can_handle(info)) {
        std::cout << "使用处理器: " << handler->get_name() << std::endl;
        handler->handle(info);
        return true;
      }
    }
    std::cout << "没有找到合适的任务处理器" << std::endl;
    return false;
  }

  // 静态方法，避免重复创建对象
  static bool process_task(Info_net_s *info) {
    static TaskDispatcher dispatcher; // 修正变量名
    return dispatcher.dispatch(info);
  }
};