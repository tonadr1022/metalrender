#pragma once

#include <BS_thread_pool.hpp>

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

class ThreadPool {
 public:
  static BS::thread_pool<BS::tp::none>& get() {
    static BS::thread_pool<BS::tp::none> pool;
    return pool;
  }
};

} // namespace TENG_NAMESPACE
