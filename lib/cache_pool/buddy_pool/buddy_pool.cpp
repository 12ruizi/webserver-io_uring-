#include "buddy_pool.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

Buffer_BuddySystem::Buffer_BuddySystem(size_t pool_size, size_t min_block_size)
    : total_pool_size(pool_size), min_block_size(min_block_size) {

  // 计算最大级别
  max_order = 0;
  size_t block_size = min_block_size;
  while (block_size <= pool_size) {
    max_order++;
    block_size *= 2;
  }
  max_order--; // 调整到合适的级别

  // 初始化空闲链表数组
  free_lists.resize(max_order + 1, nullptr);

  // 分配内存池
  memory_pool = new char[total_pool_size];
  if (!memory_pool) {
    throw std::bad_alloc();
  }

  // 创建最大的块并加入空闲链表
  BuddyBlock *initial_block =
      new BuddyBlock(memory_pool, total_pool_size, max_order);
  free_lists[max_order] = initial_block;

  std::cout << "伙伴系统初始化完成: 总大小=" << total_pool_size
            << "字节, 最小块大小=" << min_block_size
            << "字节, 最大级别=" << max_order << std::endl;
}

Buffer_BuddySystem::~Buffer_BuddySystem() {
  std::lock_guard<std::mutex> lock(pool_mutex);

  // 释放所有BuddyBlock对象
  for (size_t i = 0; i <= max_order; ++i) {
    BuddyBlock *current = free_lists[i];
    while (current) {
      BuddyBlock *next = current->next;
      delete current;
      current = next;
    }
  }

  // 释放内存池
  delete[] memory_pool;
}

size_t Buffer_BuddySystem::calculate_order(size_t size) const {
  if (size < min_block_size) {
    size = min_block_size;
  }

  size_t order = 0;
  size_t block_size = min_block_size;

  while (block_size < size && order < max_order) {
    order++;
    block_size *= 2;
  }

  return order;
}

char *Buffer_BuddySystem::allocate_buffer(size_t size) {
  std::lock_guard<std::mutex> lock(pool_mutex);

  if (size == 0 || size > total_pool_size) {
    return nullptr;
  }

  size_t required_order = calculate_order(size);

  // 查找合适级别的空闲块
  size_t current_order = required_order;
  while (current_order <= max_order) {
    if (free_lists[current_order] != nullptr) {
      break;
    }
    current_order++;
  }

  if (current_order > max_order) {
    // 没有足够大的块
    return nullptr;
  }

  // 如果找到的块比需要的大，需要分割
  while (current_order > required_order) {
    split_block(current_order);
    current_order--;
  }

  // 分配块
  BuddyBlock *block = free_lists[required_order];
  free_lists[required_order] = block->next;
  block->is_free = false;
  block->next = nullptr;

  // 清零分配的内存
  std::memset(block->base_address, 0, block->size);
  std::cout << "分配成功" << block->base_address << std::endl;
  return block->base_address;
}

void Buffer_BuddySystem::split_block(size_t order) {
  if (order == 0 || free_lists[order] == nullptr) {
    return;
  }

  BuddyBlock *block = free_lists[order];
  free_lists[order] = block->next;

  // 创建两个小一级的块
  size_t new_size = block->size / 2;
  size_t new_order = order - 1;

  BuddyBlock *left_block =
      new BuddyBlock(block->base_address, new_size, new_order);
  BuddyBlock *right_block =
      new BuddyBlock(block->base_address + new_size, new_size, new_order);

  // 设置伙伴关系
  left_block->buddy = right_block;
  right_block->buddy = left_block;

  // 将新块加入空闲链表
  left_block->next = free_lists[new_order];
  free_lists[new_order] = left_block;

  right_block->next = free_lists[new_order];
  free_lists[new_order] = right_block;

  // 删除原来的大块
  delete block;
}

bool Buffer_BuddySystem::deallocate_buffer(char *ptr) {
  std::lock_guard<std::mutex> lock(pool_mutex);

  if (ptr < memory_pool || ptr >= memory_pool + total_pool_size) {
    return false;
  }

  // 查找对应的块（简化实现，实际应该维护分配块的映射）
  // 这里我们遍历所有可能的块来查找
  for (size_t order = 0; order <= max_order; ++order) {
    size_t block_size = min_block_size * (1 << order);
    BuddyBlock *current = free_lists[order];
    BuddyBlock *prev = nullptr;

    while (current) {
      if (!current->is_free && current->base_address == ptr) {
        // 找到对应的块
        current->is_free = true;

        // 从当前位置移除
        if (prev) {
          prev->next = current->next;
        } else {
          free_lists[order] = current->next;
        }

        // 加入空闲链表并尝试合并
        current->next = free_lists[order];
        free_lists[order] = current;

        // 尝试合并伙伴块
        merge_buddies(current);
        return true;
      }
      prev = current;
      current = current->next;
    }
  }

  return false;
}

void Buffer_BuddySystem::merge_buddies(BuddyBlock *block) {
  if (!block || !block->buddy || !block->buddy->is_free) {
    return;
  }

  BuddyBlock *buddy = block->buddy;

  // 检查是否可以合并（同一级别且都空闲）
  if (block->order == buddy->order && block->is_free && buddy->is_free) {
    size_t merged_order = block->order + 1;

    if (merged_order > max_order) {
      return; // 不能超过最大级别
    }

    // 确定左右块
    BuddyBlock *left_block =
        (block->base_address < buddy->base_address) ? block : buddy;
    BuddyBlock *right_block =
        (block->base_address < buddy->base_address) ? buddy : block;

    // 从当前级别链表中移除两个块
    BuddyBlock *current = free_lists[block->order];
    BuddyBlock *prev = nullptr;
    while (current) {
      if (current == left_block || current == right_block) {
        if (prev) {
          prev->next = current->next;
        } else {
          free_lists[block->order] = current->next;
        }
      } else {
        prev = current;
      }
      current = current->next;
    }

    // 创建合并后的大块
    BuddyBlock *merged_block = new BuddyBlock(
        left_block->base_address, left_block->size * 2, merged_order);

    // 加入更高级别的空闲链表
    merged_block->next = free_lists[merged_order];
    free_lists[merged_order] = merged_block;

    // 删除原来的小块
    delete left_block;
    delete right_block;

    // 递归尝试继续合并
    merge_buddies(merged_block);
  }
}

void Buffer_BuddySystem::defragment() {
  std::lock_guard<std::mutex> lock(pool_mutex);

  // 尝试合并所有可能的伙伴块
  for (size_t order = 0; order < max_order; ++order) {
    BuddyBlock *current = free_lists[order];
    while (current) {
      if (current->is_free) {
        merge_buddies(current);
      }
      current = current->next;
    }
  }
}

size_t Buffer_BuddySystem::get_fragmentation() {
  std::lock_guard<std::mutex> lock(pool_mutex);

  size_t free_blocks = 0;
  size_t total_free_memory = 0;

  for (size_t order = 0; order <= max_order; ++order) {
    BuddyBlock *current = free_lists[order];
    while (current) {
      if (current->is_free) {
        free_blocks++;
        total_free_memory += current->size;
      }
      current = current->next;
    }
  }

  if (free_blocks <= 1) {
    return 0;
  }

  // 碎片率 = 空闲块数量 / (总空闲内存 / 最小块大小)
  return (free_blocks * 100) / (total_free_memory / min_block_size);
}

size_t Buffer_BuddySystem::get_available_memory() {
  std::lock_guard<std::mutex> lock(pool_mutex);

  size_t available = 0;
  for (size_t order = 0; order <= max_order; ++order) {
    BuddyBlock *current = free_lists[order];
    while (current) {
      if (current->is_free) {
        available += current->size;
      }
      current = current->next;
    }
  }

  return available;
}

void Buffer_BuddySystem::print_memory_status() {
  std::lock_guard<std::mutex> lock(pool_mutex);

  std::cout << "=== 伙伴系统内存状态 ===" << std::endl;
  std::cout << "总内存大小: " << total_pool_size << " 字节" << std::endl;
  std::cout << "最小块大小: " << min_block_size << " 字节" << std::endl;
  std::cout << "最大级别: " << max_order << std::endl;

  size_t total_free = 0;
  size_t total_used = 0;

  for (size_t order = 0; order <= max_order; ++order) {
    size_t block_size = min_block_size * (1 << order);
    size_t free_count = 0;
    size_t used_count = 0;

    BuddyBlock *current = free_lists[order];
    while (current) {
      if (current->is_free) {
        free_count++;
        total_free += block_size;
      } else {
        used_count++;
        total_used += block_size;
      }
      current = current->next;
    }

    if (free_count > 0 || used_count > 0) {
      std::cout << "级别 " << order << " (" << block_size << " 字节): "
                << "空闲=" << free_count << "块, 使用=" << used_count << "块"
                << std::endl;
    }
  }

  std::cout << "总空闲内存: " << total_free << " 字节" << std::endl;
  std::cout << "总使用内存: " << total_used << " 字节" << std::endl;
  std::cout << "碎片率: " << get_fragmentation() << "%" << std::endl;
  std::cout << "=========================" << std::endl;
}