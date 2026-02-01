#include "slab_pool.h"
#include <iostream>

// SlabCache析构函数实现
template <class T> SlabConnectionPool<T>::SlabCache::~SlabCache() {
  // 清理所有Slab
  slab *current = partial_slabs;
  while (current) {
    slab *next = current->next;
    delete current;
    current = next;
  }

  current = complete_slabs;
  while (current) {
    slab *next = current->next;
    delete current;
    current = next;
  }

  current = empty_slabs;
  while (current) {
    slab *next = current->next;
    delete current;
    current = next;
  }
}

// 预分配slab实现
template <class T> void SlabConnectionPool<T>::preallocate_slabs(size_t count) {
  std::lock_guard<std::mutex> lock(_cache.cache_mutex);
  for (size_t i = 0; i < count; ++i) {
    slab *new_slab = new slab(64); // 每个slab 64个对象
    new_slab->next = _cache.empty_slabs;
    _cache.empty_slabs = new_slab;
    _total_objects += 64;
  }
}

// 统计slab数量实现
template <class T> size_t SlabConnectionPool<T>::count_slab(slab *head) const {
  size_t count = 0;
  slab *current = head;
  while (current) {
    count++;
    current = current->next;
  }
  return count;
}

// 从缓冲中分配对象实现
template <class T> T *SlabConnectionPool<T>::allocate_from_cache() {
  std::lock_guard<std::mutex> lock(_cache.cache_mutex);
  // 从部分开始拿
  slab *slab_ptr = _cache.partial_slabs;
  slab *prev = nullptr;

  while (slab_ptr) {
    int index = slab_ptr->find_first_free();
    if (index != -1) {
      // 找到空闲对象
      slab_ptr->free_set.reset(index);
      slab_ptr->free_cout--;

      // 如果Slab变为完全使用，则移动到full链表；
      if (slab_ptr->is_completely_used()) {
        if (prev) {
          prev->next = slab_ptr->next;
        } else {
          _cache.partial_slabs = slab_ptr->next;
        }
        slab_ptr->next = _cache.complete_slabs;
        _cache.complete_slabs = slab_ptr;
      }
      return &slab_ptr->objects[index];
    }
    prev = slab_ptr;
    slab_ptr = slab_ptr->next;
  }

  // 如果没找到,尝试从空的slab分配
  if (_cache.empty_slabs) {
    slab_ptr = _cache.empty_slabs;
    _cache.empty_slabs = slab_ptr->next;
    int index = slab_ptr->find_first_free();
    if (index != -1) {
      slab_ptr->free_set.reset(index);
      slab_ptr->free_cout--;

      // 添加到部分使用的链表中
      slab_ptr->next = _cache.partial_slabs;
      _cache.partial_slabs = slab_ptr;
      _total_objects += 64;
      return &slab_ptr->objects[index];
    }
  }

  // 如果还没有可用的对象，创建新的slab
  if (_total_objects.load() < _max_objects_) {
    if (new_slab_count()) {
      slab_ptr = _cache.empty_slabs;
      if (slab_ptr) {
        _cache.empty_slabs = slab_ptr->next;
        int index = slab_ptr->find_first_free();
        if (index != -1) {
          slab_ptr->free_set.reset(index);
          slab_ptr->free_cout--;

          // 添加到部分使用的链表中
          slab_ptr->next = _cache.partial_slabs;
          _cache.partial_slabs = slab_ptr;
          _total_objects += 64;
          return &slab_ptr->objects[index];
        }
      }
    }
  }
  return nullptr;
}

// new_slab_count实现
template <class T> bool SlabConnectionPool<T>::new_slab_count() {
  size_t count = _total_objects.load();
  for (; count < _max_objects_; count += 64) {
    break;
  }

  size_t slab_count = (count - 64 - _total_objects.load()) / 68;
  if (slab_count == 0) {
    return false;
  } else if (slab_count == 1) {
    preallocate_slabs(1);
  } else {
    slab_count = slab_count / 2;
  }

  for (size_t i = 0; i < slab_count; i++) {
    preallocate_slabs(1);
  }
  return true;
}

// 释放对象到缓存实现
template <class T> void SlabConnectionPool<T>::deallocate_to_cache(T *conn) {
  std::lock_guard<std::mutex> cache_lock(_cache.cache_mutex);

  // 计算对象在slab中的位置
  slab *slab_ptr = find_slab_containing(conn);
  if (!slab_ptr) {
    return;
  }

  int index = conn - &slab_ptr->objects[0];
  if (index >= 64) {
    return;
  }

  // 标记对象为空闲
  slab_ptr->free_set.set(index);
  slab_ptr->free_cout++;

  // 根据slab状态重新分类；
  reclassify_slab(slab_ptr);
}

// 查找包含指定对象的slab实现
template <class T>
typename SlabConnectionPool<T>::slab *
SlabConnectionPool<T>::find_slab_containing(T *conn) {
  // 搜索部分使用链表
  slab *slab_ptr = _cache.partial_slabs;
  while (slab_ptr) {
    if (conn >= &slab_ptr->objects[0] && conn < &slab_ptr->objects[64]) {
      return slab_ptr;
    }
    slab_ptr = slab_ptr->next;
  }

  // 搜索完全使用链表
  slab_ptr = _cache.complete_slabs;
  while (slab_ptr) {
    if (conn >= &slab_ptr->objects[0] && conn < &slab_ptr->objects[64]) {
      return slab_ptr;
    }
    slab_ptr = slab_ptr->next;
  }

  return nullptr;
}

// 重新分类链表实现
template <class T> void SlabConnectionPool<T>::reclassify_slab(slab *slab_ptr) {
  // 从当前链表中移除
  remove_slab_from_list(slab_ptr);

  // 根据状态重新添加到相应链表
  if (slab_ptr->is_completely_free()) {
    slab_ptr->next = _cache.empty_slabs;
    _cache.empty_slabs = slab_ptr;
  } else if (slab_ptr->is_completely_used()) {
    slab_ptr->next = _cache.complete_slabs;
    _cache.complete_slabs = slab_ptr;
  } else {
    slab_ptr->next = _cache.partial_slabs;
    _cache.partial_slabs = slab_ptr;
  }
}

// 从链表中移除slab实现
template <class T>
void SlabConnectionPool<T>::remove_slab_from_list(slab *slabe) {
  // 从部分使用链表移除
  slab *prev = nullptr;
  slab *current = _cache.partial_slabs;
  while (current) {
    if (current == slabe) {
      if (prev) {
        prev->next = current->next;
      } else {
        _cache.partial_slabs = current->next;
      }
      break;
    }
    prev = current;
    current = current->next;
  }

  // 从完全使用链表移除
  prev = nullptr;
  current = _cache.complete_slabs;
  while (current) {
    if (current == slabe) {
      if (prev) {
        prev->next = current->next;
      } else {
        _cache.complete_slabs = current->next;
      }
      break;
    }
    prev = current;
    current = current->next;
  }

  // 从空链表移除
  prev = nullptr;
  current = _cache.empty_slabs;
  while (current) {
    if (current == slabe) {
      if (prev) {
        prev->next = current->next;
      } else {
        _cache.empty_slabs = current->next;
      }
      break;
    }
    prev = current;
    current = current->next;
  }
}

// 构造函数实现
template <class T>
SlabConnectionPool<T>::SlabConnectionPool(size_t max_objects)
    : _max_objects_(max_objects) {
  // 预分配一些初始slab
  preallocate_slabs(2);
}

// 析构函数实现
template <class T> SlabConnectionPool<T>::~SlabConnectionPool() {
  // SlabCache的析构函数会自动清理所有slab
}

// 获取连接对象实现
template <class T> T *SlabConnectionPool<T>::acquire() {
  std::lock_guard<std::mutex> lock(_pool_mutex);
  if (_active_objects.load() >= _max_objects_) {
    return nullptr;
  }

  T *conn = allocate_from_cache();
  if (conn) {
    _active_objects++;
    return conn;
  }
  return nullptr;
}

// 释放连接实现
template <class T> void SlabConnectionPool<T>::release(T *conn) {
  if (!conn) {
    return;
  }

  std::lock_guard<std::mutex> lock(_pool_mutex);
  // 释放到内存里面
  deallocate_to_cache(conn);
  _active_objects--;
}

// 直接打印统计信息实现 - 更实用的方式
template <class T> void SlabConnectionPool<T>::print_stats() {
  std::lock_guard<std::mutex> lock(_pool_mutex);

  size_t total_objects = _total_objects.load();
  size_t active_objects = _active_objects.load();
  size_t available_objects = total_objects - active_objects;

  // 统计Slab状态
  size_t partial_slabs = count_slabs(_cache.partial_slabs);
  size_t full_slabs = count_slabs(_cache.complete_slabs);
  size_t empty_slabs = count_slabs(_cache.empty_slabs);
  size_t total_slabs = partial_slabs + full_slabs + empty_slabs;

  std::cout << "=== Slab Memory Pool Statistics ===" << std::endl;
  std::cout << "总对象数: " << total_objects << std::endl;
  std::cout << "活动对象数: " << active_objects << std::endl;
  std::cout << "可用对象数: " << available_objects << std::endl;
  std::cout << "使用率: " << (active_objects * 100.0 / total_objects) << "%"
            << std::endl;
  std::cout << "总Slab数: " << total_slabs << std::endl;
  std::cout << "部分使用Slab: " << partial_slabs << std::endl;
  std::cout << "完全使用Slab: " << full_slabs << std::endl;
  std::cout << "空闲Slab: " << empty_slabs << std::endl;
  std::cout << "==================================" << std::endl;
}
