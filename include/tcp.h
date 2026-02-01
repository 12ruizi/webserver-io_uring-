#pragma once

// C系统头文件
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
// C++标准库头文件
#include <iostream>
#include <string>
// TCP模块专用配置
#define TCP_BACKLOG 128

class TcpListener {
private:
  int _port;
  int _socket_fd;

public:
  TcpListener(int port = 2025) : _port(port), _socket_fd(-1) {}

  int initialize() {
    // 创建套接字
    _socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (_socket_fd < 0) {
      perror("socket");
      return -1;
    }

    // 设置套接字选项
    int opt = 1;
    if (setsockopt(_socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) <
        0) {
      perror("setsockopt");
      close(_socket_fd);
      return -1;
    }

    // 绑定地址
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(_port);

    if (bind(_socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
      perror("bind");
      close(_socket_fd);
      return -1;
    }

    // 设置为非阻塞模式
    if (fcntl(_socket_fd, F_SETFL, O_NONBLOCK) < 0) {
      perror("fcntl");
      close(_socket_fd);
      return -1;
    }

    // 开始监听
    if (listen(_socket_fd, TCP_BACKLOG) < 0) {
      perror("listen");
      close(_socket_fd);
      return -1;
    }

    std::cout << "TCP监听器初始化成功，端口: " << _port << std::endl;
    return _socket_fd;
  }

  int get_listen_fd() {
    if (_socket_fd < 0) {
      return initialize();
    }
    return _socket_fd;
  }

  ~TcpListener() {
    if (_socket_fd != -1) {
      close(_socket_fd);
      std::cout << "TCP监听器已关闭" << std::endl;
    }
  }

  // 禁用拷贝构造和赋值
  TcpListener(const TcpListener &) = delete;
  TcpListener &operator=(const TcpListener &) = delete;
};