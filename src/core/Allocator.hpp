#pragma once

#include <map>
#include <vector>

#include "Logger.hpp"

class IndexAllocator {
 public:
  IndexAllocator() = default;
  explicit IndexAllocator(uint32_t capacity) { reserve(capacity); }

  void reserve(uint32_t capacity) {
    assert(capacity >= capacity_);
    for (uint32_t i = capacity_; i < capacity; i++) {
      free_list_.push_back(capacity - 1 - i);
    }
    capacity_ = capacity;
  }

  [[nodiscard]] uint32_t get_capacity() const { return capacity_; }

  uint32_t alloc_idx() {
    if (free_list_.empty()) {
      throw new std::runtime_error("index allocator out of space");
    }
    const uint32_t val = free_list_.back();
    free_list_.pop_back();
    return val;
  }

  void free_idx(uint32_t idx) { free_list_.push_back(idx); }

 private:
  uint32_t capacity_{};
  std::vector<uint32_t> free_list_;
};

class FreeListAllocator {
 public:
  FreeListAllocator() = default;
  FreeListAllocator(uint32_t size_bytes, uint32_t alignment) { init(size_bytes, alignment); }
  FreeListAllocator(const FreeListAllocator&) = delete;
  FreeListAllocator(FreeListAllocator&&) = delete;
  FreeListAllocator& operator=(const FreeListAllocator&) = delete;
  FreeListAllocator& operator=(FreeListAllocator&&) = delete;

  struct Slot {
    friend class FreeListAllocator;
    Slot() = default;
    [[nodiscard]] bool valid() const { return size_ != 0; }
    [[nodiscard]] uint32_t offset() const { return offset_; }
    [[nodiscard]] uint32_t size() const { return size_; }
    [[nodiscard]] uint32_t offset_plus_size() const { return offset_ + size_; }

   private:
    Slot(uint32_t offset, uint32_t size) : offset_(offset), size_(size) {}

    uint32_t offset_{};
    uint32_t size_{};
  };

  void init(uint32_t size_bytes, uint32_t alignment) {
    alignment_ = alignment;
    size_bytes = align_size(size_bytes);
    capacity_ = size_bytes;
    free_list_.emplace(size_bytes, Slot{0, size_bytes});
    initialized_ = true;
  }

  [[nodiscard]] constexpr uint32_t alloc_size() const { return sizeof(Slot); }

  [[nodiscard]] uint32_t capacity() const { return capacity_; }

  [[nodiscard]] Slot allocate(uint32_t size_bytes) {
    assert(!free_list_.empty());
    assert(initialized_);
    if (!initialized_) {
      LERROR("FreeListAllocator not initialized");
      exit(1);
    }

    size_bytes = align_size(size_bytes);

    auto smallest_slot_it = free_list_.end();
    for (auto it = free_list_.begin(); it != free_list_.end(); it++) {
      if (it->second.size() >= size_bytes) {
        smallest_slot_it = it;
        break;
      }
    }

    if (smallest_slot_it == free_list_.end()) {
      auto new_slot = Slot{capacity_, size_bytes};
      slot_offset_to_slot_.emplace(new_slot.offset(), new_slot);
      capacity_ += size_bytes;
      return new_slot;
    }

    // Don't need to split, exact size match
    if (smallest_slot_it->second.size() == size_bytes) {
      Slot return_slot = smallest_slot_it->second;
      free_list_.erase(smallest_slot_it);
      slot_offset_to_slot_.erase(return_slot.offset());
      return return_slot;
    }

    // split the slot
    auto existing_slot = smallest_slot_it->second;
    free_list_.erase(smallest_slot_it);
    auto result_slot = Slot{existing_slot.offset(), size_bytes};
    auto new_free_slot =
        Slot{result_slot.offset() + result_slot.size(), existing_slot.size() - size_bytes};
    slot_offset_to_slot_.at(new_free_slot.offset()) = new_free_slot;
    free_list_.emplace(new_free_slot.size(), new_free_slot);
    return result_slot;
  }

  // returns number of bytes freed
  uint32_t free(Slot slot) {
    free_list_.emplace(slot.size(), slot);
    return slot.size();
  }

 private:
  [[nodiscard]] size_t align_size(size_t size_bytes) const {
    return (size_bytes + (alignment_ - (size_bytes % alignment_))) % alignment_;
  }

  uint32_t alignment_{};
  uint32_t capacity_{};
  using SlotSize = uint32_t;
  std::multimap<SlotSize, Slot> free_list_;
  using SlotOffset = uint32_t;
  std::map<SlotOffset, Slot> slot_offset_to_slot_;
  bool initialized_{};
};
