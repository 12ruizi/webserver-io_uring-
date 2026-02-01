#pragma once

// C++标准库头文件
#include <mutex>
#include <vector>

// 伙伴系统模块专用配置
#define BUDDY_POOL_MIN_BLOCK_SIZE 4 * 1024
#define BUDDY_POOL_DEFAULT_SIZE 1024 * 1024

//缓冲区层，伙伴系统，索引方式是分离式链表
class Buffer_BuddySystem {
private:
  //伙伴系统块结构
  struct BuddyBlock {
    char *base_address;
    size_t size;
    size_t order; //块的级别（0最小，max_order最大）
    bool is_free;
    BuddyBlock *buddy;
    BuddyBlock *next;

    BuddyBlock(char *addr, size_t block_size, size_t block_order)
        : base_address(addr), size(block_size), order(block_order),
          is_free(true), buddy(nullptr), next(nullptr) {}
  };

  //空闲链表数组，每一个级别一个链表
  std::vector<BuddyBlock *> free_lists;
  char *memory_pool; //开始地址
  size_t total_pool_size;
  size_t min_block_size;
  size_t max_order;
  std::mutex pool_mutex; //线程安全锁

  //私有辅助方法
  size_t calculate_order(size_t size) const;
  BuddyBlock *find_buddy(BuddyBlock *block);
  void merge_buddies(BuddyBlock *block);
  void split_block(size_t required_order);

public:
  Buffer_BuddySystem(size_t pool_size,
                     size_t min_block_size = BUDDY_POOL_MIN_BLOCK_SIZE);
  ~Buffer_BuddySystem();

  char *allocate_buffer(size_t size);
  bool deallocate_buffer(char *ptr);
  void defragment();
  size_t get_fragmentation();
  size_t get_available_memory();
  void print_memory_status();
};