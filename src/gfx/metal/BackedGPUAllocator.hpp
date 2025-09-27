#pragma once

#include <Metal/MTLBuffer.hpp>
#include <bit>
#include <offsetAllocator.hpp>

#include "gfx/Device.hpp"

class BackedGPUAllocator {
 public:
  explicit BackedGPUAllocator(rhi::Device& device, const rhi::BufferDesc& buffer_desc,
                              size_t bytes_per_element)
      : backing_buffer_(device.create_buffer_h(buffer_desc)),
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

  OffsetAllocator::Allocation allocate(uint32_t element_count) {
    const OffsetAllocator::Allocation alloc = allocator_.allocate(element_count);
    if (alloc.offset == OffsetAllocator::Allocation::NO_SPACE) {
      const auto old_cap_elements = allocator_.capacity();
      const auto required_size = std::bit_ceil(std::max(element_count, allocator_.capacity() * 2));
      const auto new_cap_elements = required_size;
      assert(allocator_.grow(new_cap_elements - old_cap_elements));

      // new buffer
      auto new_buffer_desc = device_.get_buffer(backing_buffer_)->desc();
      new_buffer_desc.size = new_cap_elements * bytes_per_element_;
      auto new_buf_handle = device_.create_buffer_h(new_buffer_desc);
      auto* new_buf = device_.get_buffer(new_buf_handle);
      assert(new_buf->contents());

      // copy data
      memcpy(new_buf->contents(), device_.get_buffer(backing_buffer_)->contents(),
             old_cap_elements * bytes_per_element_);

      // update handle
      backing_buffer_ = std::move(new_buf_handle);

      // try again
      return allocate(element_count);
    }
    return alloc;
  }

  [[nodiscard]] rhi::Buffer* get_buffer() const { return device_.get_buffer(backing_buffer_); }

  void free(OffsetAllocator::Allocation alloc) { allocator_.free(alloc); }

 private:
  rhi::BufferHandleHolder backing_buffer_;
  OffsetAllocator::Allocator allocator_;
  size_t bytes_per_element_{};
  rhi::Device& device_;
};
