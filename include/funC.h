#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring> // 修复：使用cstring替代string.h
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <liburing.h>
#include <liburing/io_uring.h>
#include <map>
#include <memory>
#include <mutex>
#include <mutex> // 添加mutex头文件
#include <netinet/in.h>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <string_view>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#define BUFFER_LENGTH 32768
#define MAX_Queue 1024
#define MAX_CONNECTIONS 1024
typedef int (*CALLBACK)(int fd);
// 检查错误  如果 ==-1就返回错误  c语言写法
#define ERROR_CHECK(ret, num, msg)                                             \
  {                                                                            \
    if (ret == num) {                                                          \
      perror(msg);                                                             \
      return -1;                                                               \
    }                                                                          \
  }
#define TIME_SUB_MS(tv1, tv2)                                                  \
  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)

typedef enum {
  CONN_ACCEPT, //等待新的连接
  CONN_READ,   //等待读取数据
  CONN_WRITE,  //等待写入数据
  CONN_CLOSE   //等待关闭连接
} conn_state_t;
typedef enum {
  TASK_NONE,          //没有任务
  TASK_HTTP,          // http请求处理
  TASK_FILE_UPLOAD,   //文件上传
  TASK_FILE_DOWNLOAD, //文件下载
  TASK_CHAT           //聊天室
} task_type_t;
//用户态环形缓冲区
class Ringbuffer {
private:
  std::vector<char> _buffer;
  const size_t _capacity;
  std::atomic<size_t> _head;
  std::atomic<size_t> _tail;
  std::atomic<size_t> _hightPower;
  std::atomic<size_t> _lowPower;

public:
  explicit Ringbuffer(size_t capacity)
      : _capacity(capacity), _head(0), _tail(0), _hightPower(0), _lowPower(0) {
    _buffer.resize(_capacity);
    //高水位控制
    _hightPower = _capacity / 2;
    //低水位控制
    _lowPower = _capacity / 4;
  }
  //获取当前可写入的尾部指针
  char *get_write_tail() {
    return _buffer.data() + _tail.load(std::memory_order_acquire);
  }
  //获取当前可写入的空间大小
  size_t get_writable_size() const {
    size_t head = _head.load(std::memory_order_acquire);
    size_t tail = _tail.load(std::memory_order_acquire);
    if (tail >= head) {
      return _capacity - tail - (head == 0 ? 1 : 0); //留一个字节避免头尾重叠；
    } else {
      return head - tail - 1; //留一个字节作为边界
    }
  }
  //写入操作 -原子更新尾部指针
  bool write_date(size_t bytes_written) {
    if (bytes_written == 0) {
      return true;
    }
    size_t old_tail = _tail.load(std::memory_order_acquire);
    size_t new_tail = (old_tail + bytes_written) % _capacity;

    if (new_tail == _head.load(std::memory_order_acquire)) {
      return false;
    }
    //更新尾部指针
    _tail.store(new_tail, std::memory_order_release);
    return true;
  }
  //获取当前可读取的头部指针
  char *get_read_head() {
    return _buffer.data() + _head.load(std::memory_order_acquire);
  }
  //获取当前可读取的空间大小
  size_t get_readable_size() const {
    size_t head = _head.load(std::memory_order_acquire);
    size_t tail = _tail.load(std::memory_order_acquire);
    if (tail >= head) {
      return tail - head;
    } else {
      return _capacity - head + tail;
    }
  }
  //读取操作 -原子更新头部指针
  bool read_date(size_t bytes_read) {
    if (bytes_read == 0) {
      return true;
    }
    size_t current_head = _head.load(std::memory_order_acquire);
    size_t current_tail = _tail.load(std::memory_order_acquire);
    //检查是否又足够的空间
    size_t available;
    if (current_tail >= current_head) {
      available = current_tail - current_head;
    } else {
      available = _capacity - current_head + current_tail;
    }
    if (bytes_read > available) {
      return false;
    }
    //更新头部指针
    size_t new_head = current_head + bytes_read;
    _head.store(new_head, std::memory_order_release);
    return true;
  }
  //判断缓冲区是否为空
  bool is_empty() const {
    return _head.load(std::memory_order_acquire) ==
           _tail.load(std::memory_order_acquire);
  }
  //判断缓冲区是否已满
  bool is_full() const {
    return (_tail.load(std::memory_order_acquire) + 1) % _capacity ==
           _head.load(std::memory_order_acquire);
  }
  //获取缓冲区的总容量
  size_t get_capacity() const { return _capacity; }

  //清空缓冲区
  void clear() {
    _head.store(0, std::memory_order_release);
    _tail.store(0, std::memory_order_release);
  }
};

struct Info_net_s {
  int fd;                  //套接字描述符
  struct sockaddr_in addr; //客户端地址信息
  socklen_t addrlen;
  Ringbuffer rubffer;
  size_t rubuffer_processed; //已经处理的长度
  Ringbuffer wbuffer;
  conn_state_t state; // fd什么事件
  Info_net_s()
      : fd(-1), addrlen(sizeof(addr)), rubffer(BUFFER_LENGTH),
        rubuffer_processed(0), wbuffer(BUFFER_LENGTH), state(CONN_ACCEPT) {}
};
