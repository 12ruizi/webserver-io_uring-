#include "uring_server.h"
#include <csignal>
#include <iostream>

// 全局服务器实例指针
IoUringServer *g_server = nullptr;

// 信号处理函数
void signal_handler(int signal) {
  std::cout << "\n接收到信号 " << signal << "，正在关闭服务器..." << std::endl;
  if (g_server) {
    g_server->stop();
  }
}

int main() {
  // 设置信号处理
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  try {
    std::cout << "启动IO_URING服务器..." << std::endl;

    // 创建服务器实例
    IoUringServer server(2025); // 使用2025端口
    g_server = &server;

    std::cout << "服务器初始化完成，开始运行..." << std::endl;
    std::cout << "按 Ctrl+C 停止服务器" << std::endl;

    // 运行服务器
    server.run();

    std::cout << "服务器已正常关闭" << std::endl;

  } catch (const std::exception &e) {
    std::cerr << "服务器运行错误: " << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "未知错误发生" << std::endl;
    return 1;
  }

  return 0;
}