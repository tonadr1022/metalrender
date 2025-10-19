#pragma once

#include <BS_thread_pool.hpp>

class ThreadPool {
 public:
  static BS::thread_pool<BS::tp::none>& get() {
    static BS::thread_pool<BS::tp::none> pool;
    return pool;
  }
};
