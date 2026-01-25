#include "funC.h"
#include <fcntl.h>
#include <iostream>
#include <unistd.h>
#if 1
class Tcp_listen {
private:
  int _port;
  int _sockFd;

public:
  Tcp_listen(int port = 2025) : _port(port), _sockFd(-1){};
  int init_TCP() {
    //创建套接字 opt bind and listen
    _sockFd = socket(AF_INET, SOCK_STREAM, 0);
    if (_sockFd >= 0) {

      int ret = -1;
      int opt = 1;
      ret = setsockopt(_sockFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
      if (ret >= 0) {
        struct sockaddr_in addR;
        addR.sin_family = AF_INET;
        addR.sin_addr.s_addr = INADDR_ANY;
        addR.sin_port = htons(_port);
        ret = bind(_sockFd, (struct sockaddr *)&addR, sizeof(addR));
        if (ret >= 0) {
          ret = fcntl(_sockFd, F_SETFL, O_NONBLOCK);
          if (ret >= 0) {

            ret = listen(_sockFd, 128);
            if (ret >= 0) {
              return _sockFd;
            }
          }

          perror(" listen ");
          close(_sockFd);

          return -1;
        }
        perror(" bind ");
        close(_sockFd);
        return -1;
      }
      perror(" socket opt");
      close(_sockFd);

      return -1;
    }
    perror(" socket ");
    return -1;
  }
  int get_listenFd() {

    init_TCP();
    return _sockFd;
  }
  ~Tcp_listen() {
    if (_sockFd != -1) {
      close(_sockFd);
    }
  }
};
#else
//想要换一种写法但是目前没有想到用什么办法 改变这种写法；
class Tcp_listen {
private:
  int _sockeFd;

public:
  TCP_Tcp_listen(int port = 2025) {
    _sockeFd = socket(AF_INET, SOCK_STREAM, 0);
    //
  }
}

#endif
