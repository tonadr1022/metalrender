#pragma once

#include <offsetAllocator.hpp>

#include "gfx/Buffer.hpp"
#include "gfx/Device.hpp"

namespace gfx {

struct BufferCopyMgr;

class BackedGPUAllocator {
 public:
  explicit BackedGPUAllocator(rhi::Device& device, BufferCopyMgr& buffer_copy_mgr,
                              const rhi::BufferDesc& buffer_desc, size_t bytes_per_element);

  BackedGPUAllocator(const BackedGPUAllocator& other) = delete;

  BackedGPUAllocator& operator=(const BackedGPUAllocator& other) = delete;

  BackedGPUAllocator(BackedGPUAllocator&& other) noexcept;

  BackedGPUAllocator& operator=(BackedGPUAllocator&& other) = delete;

  OffsetAllocator::Allocation allocate(uint32_t element_count, bool& resize_occured);

  [[nodiscard]] rhi::Buffer* get_buffer() const { return device_.get_buf(backing_buffer_); }
  [[nodiscard]] rhi::BufferHandle get_buffer_handle() const { return backing_buffer_.handle; }
  [[nodiscard]] const OffsetAllocator::Allocator& get_allocator() const { return allocator_; }
  [[nodiscard]] uint32_t allocated_element_count() const { return allocated_element_count_; }
  [[nodiscard]] uint32_t allocated_elements_size_bytes() const {
    return allocated_element_count_ * bytes_per_element_;
  }
  [[nodiscard]] uint32_t max_seen_size() const { return max_seen_size_; }

  void free(OffsetAllocator::Allocation alloc);

 private:
  rhi::BufferHandleHolder backing_buffer_;
  OffsetAllocator::Allocator allocator_;
  size_t bytes_per_element_{};
  uint32_t max_seen_size_{};
  rhi::Device& device_;
  uint32_t allocated_element_count_{};
  BufferCopyMgr& buffer_copy_mgr_;
};

}  // namespace gfx
