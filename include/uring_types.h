#pragma once
// C系统头文件
#include <liburing.h>
#include <liburing/io_uring.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

// C++标准库头文件
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <time.h>
#include <vector>
// io_uring模块专用配置
#define URING_MAX_QUEUE 1024
#define URING_thread_MAX_QUEUE 1024
#define URING_MAX_CONNECTIONS 1024
#define URING_BUFFER_SIZE 32768
#define URING_DEFAULT_THREAD_COUNT 10
#define TCP_DEFAULT_PORT 2025
#define MAX_CACHE_SIZE (1024 * 1024)
#define MIN_BLOCK_SIZE (4 * 1024)
struct UringConnectionInfo;
// 连接状态枚举
enum class UringConnectionState {
  ACCEPT, // 等待新的连接
  READ,   // 等待读取数据
  WRITE,  // 等待写入数据
  CLOSE   // 等待关闭连接
};
// http报文解析枚举状态
enum class ParseResult {
  COMPLETE,
  NEEED_MORE_DATA,
  INVALID_FORMAT,
  CHUNKED_UNSUPPORTED,
};
enum class TaskType {
  HTTP,
  FILE,   // 普通任务
  CHAT,   // 关闭任务
  NOKNOW, // 未知任务
};

// 环形缓冲区类（io_uring模块专用）
class UringRingBuffer {
private:
  std::vector<char> _buffer;
  const size_t _capacity;
  std::atomic<size_t> _head;
  std::atomic<size_t> _tail;

public:
  explicit UringRingBuffer(size_t capacity)
      : _capacity(capacity), _head(0), _tail(0) {
    _buffer.resize(_capacity);
  }

  // 获取当前可写入的尾部指针
  char *get_write_tail() {
    return _buffer.data() + _tail.load(std::memory_order_acquire);
  }

  // 获取当前可写入的空间大小
  size_t get_writable_size() const {
    size_t head = _head.load(std::memory_order_acquire);
    size_t tail = _tail.load(std::memory_order_acquire);
    if (tail >= head) {
      return _capacity - tail - (head == 0 ? 1 : 0);
    } else {
      return head - tail - 1;
    }
  }

  // 写入操作
  bool write_data(size_t bytes_written) {
    if (bytes_written == 0)
      return true;
    size_t old_tail = _tail.load(std::memory_order_acquire);
    size_t new_tail = (old_tail + bytes_written) % _capacity;
    if (new_tail == _head.load(std::memory_order_acquire))
      return false;
    _tail.store(new_tail, std::memory_order_release);
    return true;
  }

  // 获取当前可读取的头部指针
  char *get_read_head() {
    return _buffer.data() + _head.load(std::memory_order_acquire);
  }

  // 获取当前可读取的空间大小
  size_t get_readable_size() const {
    size_t head = _head.load(std::memory_order_acquire);
    size_t tail = _tail.load(std::memory_order_acquire);
    if (tail >= head) {
      return tail - head;
    } else {
      return _capacity - head + tail;
    }
  }

  // 读取操作
  bool read_data(size_t bytes_read) {
    if (bytes_read == 0)
      return true;
    size_t current_head = _head.load(std::memory_order_acquire);
    size_t current_tail = _tail.load(std::memory_order_acquire);
    size_t available = (current_tail >= current_head)
                           ? current_tail - current_head
                           : _capacity - current_head + current_tail;
    if (bytes_read > available)
      return false;
    size_t new_head = (current_head + bytes_read) % _capacity;
    _head.store(new_head, std::memory_order_release);
    return true;
  }

  // 判断缓冲区是否为空
  bool is_empty() const {
    return _head.load(std::memory_order_acquire) ==
           _tail.load(std::memory_order_acquire);
  }

  // 清空缓冲区
  void clear() {
    _head.store(0, std::memory_order_release);
    _tail.store(0, std::memory_order_release);
  }

  size_t get_capacity() const { return _capacity; }
};

// 任务优先级枚举
enum class TaskPriority { HIGH = 0, NORMAL = 1, LOW = 2 };

// 任务回调函数类型定义
using TaskCallback = std::function<void(UringConnectionInfo *)>;

// 主线程任务结构体
struct MainThreadTask {
  UringConnectionInfo *conn;
  TaskCallback callback;
  TaskPriority priority;

  MainThreadTask(UringConnectionInfo *c, TaskCallback cb,
                 TaskPriority p = TaskPriority::NORMAL)
      : conn(c), callback(cb), priority(p) {}

  // 优先级比较
  bool operator<(const MainThreadTask &other) const {
    return static_cast<int>(priority) < static_cast<int>(other.priority);
  }
};

// 主线程任务队列（用于线程池回调）
class MainThreadTaskQueue {
private:
  std::queue<MainThreadTask> _tasks;
  std::mutex _mutex;
  std::condition_variable _condition;
  std::atomic<bool> _running{true};

public:
  // 添加任务到主线程队列
  bool push_task(UringConnectionInfo *conn, TaskCallback callback,
                 TaskPriority priority = TaskPriority::NORMAL) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_tasks.size() >= URING_MAX_QUEUE) {
      return false;
    }
    _tasks.emplace(conn, callback, priority);
    _condition.notify_one();
    return true;
  }

  // 获取任务（阻塞）
  MainThreadTask pop_task() {
    std::unique_lock<std::mutex> lock(_mutex);
    _condition.wait(lock, [this]() { return !_running || !_tasks.empty(); });

    if (!_running && _tasks.empty()) {
      throw std::runtime_error("Task queue stopped");
    }

    MainThreadTask task = std::move(_tasks.front());
    _tasks.pop();
    return task;
  }

  // 非阻塞获取任务
  bool try_pop_task(MainThreadTask &task) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_tasks.empty())
      return false;

    task = std::move(_tasks.front());
    _tasks.pop();
    return true;
  }

  // 停止队列
  void stop() {
    _running = false;
    _condition.notify_all();
  }

  // 获取队列大小
  size_t size() {
    std::lock_guard<std::mutex> lock(_mutex);
    return _tasks.size();
  }
};

// 网络连接信息结构体
struct UringConnectionInfo {
  int fd;                       // 套接字描述符
  struct sockaddr_in addr;      // 客户端地址信息
  socklen_t addrlen;            // 地址长度
  UringRingBuffer read_buffer;  // 读缓冲区
  UringRingBuffer write_buffer; // 写缓冲区
  UringConnectionState state;   // 连接状态
  size_t bytes_NO_read;         // 还需要读的长度
  TaskType task_type;           //任务类型
  ParseResult parse_result;     // http报文解析状态
  char *extra_buffer; // 额外缓冲区，用于存储不完整的http报文
  bool extra_buffer_in_use; // 额外缓冲区是否在使用中
  std::shared_ptr<MainThreadTaskQueue> _main_queue; // 主线程任务队列引用

  UringConnectionInfo()
      : fd(-1), addrlen(sizeof(addr)), read_buffer(URING_BUFFER_SIZE),
        write_buffer(URING_BUFFER_SIZE), state(UringConnectionState::ACCEPT),
        bytes_NO_read(0), task_type(TaskType::NOKNOW),
        parse_result(ParseResult::NEEED_MORE_DATA), extra_buffer(nullptr),
        extra_buffer_in_use(false), last_active_time(0), _main_queue(nullptr) {}

  // 设置主线程队列
  void set_main_queue(std::shared_ptr<MainThreadTaskQueue> main_queue) {
    _main_queue = main_queue;
  }
};

// io_uring事件类型枚举
enum class UringEventType {
  ACCEPT_EVENT,
  READ_EVENT,
  WRITE_EVENT,
  CLOSE_EVENT,
  TIMEOUT_EVENT
};

// io_uring操作结果结构体
struct UringOperationResult {
  UringEventType event_type;
  int result_code;
  void *user_data;

  UringOperationResult()
      : event_type(UringEventType::ACCEPT_EVENT), result_code(0),
        user_data(nullptr) {}
};
