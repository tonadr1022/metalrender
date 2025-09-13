#pragma once

#include <vector>

class IndexAllocator {
 public:
  explicit IndexAllocator(uint32_t capacity) { reserve(capacity); }

  void reserve(uint32_t capacity) {
    assert(capacity >= capacity_);
    for (uint32_t i = capacity_; i < capacity; i++) {
      free_list_.push_back(capacity - 1 - i);
    }
    capacity_ = capacity;
  }

  uint32_t get_capacity() const { return capacity_; }

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
  uint32_t capacity_{};
  std::vector<uint32_t> free_list_;
};
