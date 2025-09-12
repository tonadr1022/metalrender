#pragma once

#include <vector>

class IndexAllocator {
 public:
  explicit IndexAllocator(uint32_t capacity) {
    for (uint32_t i = 0; i < capacity; i++) {
      free_list_.push_back(capacity - 1 - i);
    }
  }

  uint32_t alloc_idx() {
    if (free_list_.empty()) {
      throw new std::runtime_error("index allocator out of space");
    }
    uint32_t val = free_list_.back();
    free_list_.pop_back();
    return val;
  }

  void free_idx(uint32_t idx) { free_list_.push_back(idx); }

 private:
  std::vector<uint32_t> free_list_;
};
