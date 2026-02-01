#include "BackedGPUAllocator.hpp"

#include <bit>

#include "gfx/renderer/BufferResize.hpp"

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

gfx::BackedGPUAllocator::BackedGPUAllocator(rhi::Device& device, BufferCopyMgr& buffer_copy_mgr,
                                            const rhi::BufferDesc& buffer_desc,
                                            size_t bytes_per_element)
    : buffer_desc_(buffer_desc),
      allocator_(buffer_desc.size / bytes_per_element),
      bytes_per_element_(bytes_per_element),
      device_(device),
      buffer_copy_mgr_(buffer_copy_mgr) {
  if (buffer_desc_.size > 0) {
    backing_buffer_ = device_.create_buf_h(buffer_desc_);
  }
}

OffsetAllocator::Allocation gfx::BackedGPUAllocator::allocate(uint32_t element_count,
                                                              bool& resize_occured) {
  const OffsetAllocator::Allocation alloc = allocator_.allocate(element_count);
  if (alloc.offset == OffsetAllocator::Allocation::NO_SPACE) {
    resize_occured = true;
    const auto old_cap_elements = allocator_.capacity();
    const auto required_size = std::bit_ceil(std::max(element_count, allocator_.capacity() * 2));
    const auto new_cap_elements = required_size;
    ALWAYS_ASSERT(allocator_.grow(new_cap_elements - old_cap_elements));

    auto new_buffer_desc = buffer_desc_;
    new_buffer_desc.size = new_cap_elements * bytes_per_element_;
    auto new_buf_handle = device_.create_buf_h(new_buffer_desc);
    if (backing_buffer_.is_valid()) {
      buffer_copy_mgr_.add_copy({
          .src_buf = backing_buffer_.handle,
          .dst_buf = new_buf_handle.handle,
          .size = old_cap_elements * bytes_per_element_,
      });
    }
    backing_buffer_ = std::move(new_buf_handle);

    return allocate(element_count, resize_occured);
  }
  allocated_element_count_ += element_count;
  max_seen_size_ = std::max<uint32_t>(max_seen_size_, alloc.offset + element_count);
  return alloc;
}

gfx::BackedGPUAllocator::BackedGPUAllocator(BackedGPUAllocator&& other) noexcept
    : backing_buffer_(std::exchange(other.backing_buffer_, rhi::BufferHandleHolder{})),
      allocator_(std::move(other.allocator_)),
      bytes_per_element_(std::exchange(other.bytes_per_element_, 0)),
      device_(other.device_),
      buffer_copy_mgr_(other.buffer_copy_mgr_) {}

void gfx::BackedGPUAllocator::free(OffsetAllocator::Allocation alloc) {
  if (alloc.offset != OffsetAllocator::Allocation::NO_SPACE) {
    allocated_element_count_ -= allocator_.allocationSize(alloc);
    allocator_.free(alloc);
  }
}

} // namespace TENG_NAMESPACE
