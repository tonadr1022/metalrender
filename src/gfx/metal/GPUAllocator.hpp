#pragma once

#include <Metal/MTLBuffer.hpp>
#include <offsetAllocator.hpp>

class GPUAllocator {
 public:
  explicit GPUAllocator(MTL::Buffer* buffer, size_t element_capacity, size_t bytes_per_element)
      : backing_buffer_(buffer),
        allocator_(element_capacity),
        bytes_per_element_(bytes_per_element) {}

  explicit GPUAllocator(size_t element_capacity, size_t bytes_per_element)
      : allocator_(element_capacity), bytes_per_element_(bytes_per_element) {}

  GPUAllocator(const GPUAllocator& other) = delete;

  GPUAllocator(GPUAllocator&& other) noexcept
      : backing_buffer_(std::exchange(other.backing_buffer_, nullptr)),
        allocator_(std::move(other.allocator_)),
        bytes_per_element_(std::exchange(other.bytes_per_element_, 0)) {}
  GPUAllocator& operator=(const GPUAllocator& other) = delete;
  GPUAllocator& operator=(GPUAllocator&& other) = delete;
  [[nodiscard]] MTL::Buffer* buffer() const { return backing_buffer_; }

  ~GPUAllocator() {
    if (backing_buffer_) {
      backing_buffer_->release();
    }
    backing_buffer_ = nullptr;
  }

  OffsetAllocator::Allocation allocate(size_t element_count) {
    if (allocator_.storageReport().totalFreeSpace == 0) {
      assert(0);
      // TODO: resize buffer
      return {};
    }
    return allocator_.allocate(element_count);
  }

  [[nodiscard]] size_t byte_offset(OffsetAllocator::Allocation alloc) const {
    return alloc.offset * bytes_per_element_;
  }

  void free(OffsetAllocator::Allocation alloc) { allocator_.free(alloc); }

 private:
  MTL::Buffer* backing_buffer_{};
  OffsetAllocator::Allocator allocator_;
  size_t bytes_per_element_{};
};
