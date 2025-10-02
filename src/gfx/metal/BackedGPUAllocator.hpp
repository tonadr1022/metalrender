#pragma once

#include <Metal/MTLBuffer.hpp>
#include <bit>
#include <offsetAllocator.hpp>

#include "core/EAssert.hpp"
#include "gfx/Device.hpp"

class BackedGPUAllocator {
 public:
  explicit BackedGPUAllocator(rhi::Device& device, const rhi::BufferDesc& buffer_desc,
                              size_t bytes_per_element)
      : backing_buffer_(device.create_buf_h(buffer_desc)),
        allocator_(buffer_desc.size / bytes_per_element),
        bytes_per_element_(bytes_per_element),
        device_(device) {}

  BackedGPUAllocator(const BackedGPUAllocator& other) = delete;

  BackedGPUAllocator& operator=(const BackedGPUAllocator& other) = delete;

  BackedGPUAllocator(BackedGPUAllocator&& other) noexcept
      : backing_buffer_(std::exchange(other.backing_buffer_, rhi::BufferHandleHolder{})),
        allocator_(std::move(other.allocator_)),
        bytes_per_element_(std::exchange(other.bytes_per_element_, 0)),
        device_(other.device_) {}

  BackedGPUAllocator& operator=(BackedGPUAllocator&& other) = delete;

  OffsetAllocator::Allocation allocate(uint32_t element_count, bool& resize_occured) {
    const OffsetAllocator::Allocation alloc = allocator_.allocate(element_count);
    if (alloc.offset == OffsetAllocator::Allocation::NO_SPACE) {
      resize_occured = true;
      const auto old_cap_elements = allocator_.capacity();
      const auto required_size = std::bit_ceil(std::max(element_count, allocator_.capacity() * 2));
      const auto new_cap_elements = required_size;
      ALWAYS_ASSERT(allocator_.grow(new_cap_elements - old_cap_elements));

      auto new_buffer_desc = device_.get_buf(backing_buffer_)->desc();
      new_buffer_desc.size = new_cap_elements * bytes_per_element_;
      auto new_buf_handle = device_.create_buf_h(new_buffer_desc);
      auto* new_buf = device_.get_buf(new_buf_handle);
      ALWAYS_ASSERT(new_buf->contents());

      memcpy(new_buf->contents(), device_.get_buf(backing_buffer_)->contents(),
             old_cap_elements * bytes_per_element_);

      backing_buffer_ = std::move(new_buf_handle);

      return allocate(element_count, resize_occured);
    }
    allocated_element_count_ += element_count;
    max_seen_size_ = std::max<uint32_t>(max_seen_size_, alloc.offset + element_count);
    return alloc;
  }

  [[nodiscard]] rhi::Buffer* get_buffer() const { return device_.get_buf(backing_buffer_); }
  [[nodiscard]] const OffsetAllocator::Allocator& get_allocator() const { return allocator_; }
  [[nodiscard]] uint32_t allocated_element_count() const { return allocated_element_count_; }
  [[nodiscard]] uint32_t allocated_elements_size_bytes() const {
    return allocated_element_count_ * bytes_per_element_;
  }
  [[nodiscard]] uint32_t max_seen_size() const { return max_seen_size_; }

  void free(OffsetAllocator::Allocation alloc) {
    allocated_element_count_ -= allocator_.allocationSize(alloc);
    allocator_.free(alloc);
  }

 private:
  rhi::BufferHandleHolder backing_buffer_;
  OffsetAllocator::Allocator allocator_;
  size_t bytes_per_element_{};
  uint32_t max_seen_size_{};
  rhi::Device& device_;
  uint32_t allocated_element_count_{};
};
