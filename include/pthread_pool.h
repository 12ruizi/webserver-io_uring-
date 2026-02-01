#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <uring_types.h>
#include <vector>
class ThreadPool {
private:
  std::vector<std::thread> _workers; // 工作线程
  std::queue<std::function<void()>>
      _tasks;             // 任务队列 //利用function多态函数封装器
  std::mutex _queueMutex; // 队列互斥锁
  std::condition_variable _condition; // 条件变量
  std::atomic<bool> _stop;            // 设置停止状态
  // std::shared_ptr<MainThreadTaskQueue> _main_queue; // 主线程任务队列

public:
  // 构造函数，创建指定数量的工作线程
  explicit ThreadPool(
      size_t threadCount = std::thread::
          hardware_concurrency() // 静态函数获取当前系统cpu逻辑核心数
      )
      : _stop(false) {

    if (threadCount == 0) {
      threadCount = 4; // 默认四个线程
    }
    // 构造函数里面需要为线程队列做处理
    for (size_t i = 0; i < threadCount; ++i) {
      _workers.emplace_back([this] {
        while (true) {
          std::function<void()> task; // 创建函数 任务
          {
            std::unique_lock<std::mutex> lock(this->_queueMutex);
            // 等待任务或者停止信号
            this->_condition.wait(lock, [this] { // 谓词函数用于检查得带条件
              return this->_stop ||
                     !this->_tasks
                          .empty(); // 队列为空或者原子标志为1的时候返回真
            });
            // 如果收到停止信号且任务队列为空，则线程退出；
            if (this->_stop && this->_tasks.empty()) {
              return;
            }
            // 取出一个任务
            task = std::move(this->_tasks.front());
            this->_tasks.pop();
            std::cout << "线程 " << std::this_thread::get_id()
                      << " 开始执行任务" << std::endl;
          }
          // 执行任务
          task();
        }
      });
    }
  }
  // 禁止拷贝构造和赋值
  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;
  // 提交任务到线程池，返回future 一边获取结果
  template <typename F, typename... Args>
  auto enqueue(F &&f, Args &&...args) // 这里面是参数列表
      -> std::future<std::invoke_result_t<
          F, Args...>> // 修复：直接使用std::invoke_result_t
  {                    // typename 一般是声明他是个类型
    using return_type =
        std::invoke_result_t<F, Args...>; // 修复：直接使用std::invoke_result_t
    // 将任务封装到packeaged_task中，以便获取future
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    std::future<return_type> result = task->get_future(); // 获取返回值；
    // 作用域 自动解锁
    {
      std::unique_lock<std::mutex> lock(_queueMutex); // 加锁放入队列
      // 不允许在停止线程后添加任务
      if (_stop) {
        throw std::runtime_error("enqueue on stoppend ThreadPool");
      }
      // 将任务添加到队列
      _tasks.emplace([task]() { (*task)(); });
    }
    // 通知一个线程；
    _condition.notify_one();
    return result;
  }

  // 提交带回调的任务到线程池
  template <typename F, typename... Args>
  auto
  enqueue_with_callback(F &&f, UringConnectionInfo *conn,
                        std::function<void(UringConnectionInfo *)> callback,
                        Args &&...args) -> std::future<void> {

    auto task = std::make_shared<std::packaged_task<void()>>(
        [f = std::forward<F>(f), conn, callback, args...]() mutable {
          // 执行任务
          std::invoke(std::forward<F>(f), std::forward<Args>(args)...);

          // 任务完成后，直接调用回调函数，避免通过主线程队列，
          if (callback) {
            callback(conn);
          }
        });

    std::future<void> result = task->get_future();
    {
      std::unique_lock<std::mutex> lock(_queueMutex);
      if (_stop) {
        throw std::runtime_error("enqueue on stopped ThreadPool");
      }
      _tasks.emplace([task]() { (*task)(); });
    }
    _condition.notify_one();
    return result;
  }

  // // 设置主线程任务队列，设置这个回调太慢了（运行速度）
  // void set_main_queue(std::shared_ptr<MainThreadTaskQueue> main_queue) {
  //   _main_queue = main_queue;
  // }
  // 获取当前等待执行的任务数量
  size_t pendingTasks() const {
    return _tasks
        .size(); // 注意：这个方法不是线程安全的，可能在返回时队列大小已改变
  }
  // 获取线程运行状态
  bool isRunning() const {
    return !_stop; // 停止标志取反：true表示运行中，false表示已停止
  }
  // 等待所有任务完成（但线程继续运行）
  void waitAll() {
    while (pendingTasks() > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  // 立即关闭线程池（不等待未完成的任务）
  void shutdown() {

    {
      std::unique_lock<std::mutex> lock(_queueMutex); // 加锁并设置退出标志
      _stop = true;
    } // 括号的作用是解锁
    _condition.notify_all();
    for (std::thread &worker : _workers) {
      if (worker.joinable()) { // 检查线程是否可以join
        worker.join();
      }
    }
  }
  // 析构函数
  ~ThreadPool() {
    if (!_stop) {
      shutdown();
    }
  }
};