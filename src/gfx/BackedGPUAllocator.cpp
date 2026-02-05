#include "BackedGPUAllocator.hpp"

#include <bit>

#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "gfx/renderer/BufferResize.hpp"

namespace TENG_NAMESPACE {

namespace gfx {

BackedGPUAllocator::BackedGPUAllocator(rhi::Device& device, BufferCopyMgr& buffer_copy_mgr,
                                       const rhi::BufferDesc& buffer_desc, size_t bytes_per_element)
    : buffer_desc_(buffer_desc),
      allocator_(buffer_desc.size / bytes_per_element),
      bytes_per_element_(bytes_per_element),
      device_(device),
      buffer_copy_mgr_(buffer_copy_mgr) {
  if (buffer_desc_.size > 0) {
    backing_buffer_ = device_.create_buf_h(buffer_desc_);
  }
}

OffsetAllocator::Allocation BackedGPUAllocator::allocate(uint32_t element_count,
                                                         bool& resize_occured) {
  const OffsetAllocator::Allocation alloc = allocator_.allocate(element_count);
  if (alloc.offset == OffsetAllocator::Allocation::NO_SPACE) {
    resize_occured = true;
    const auto old_cap_elements = allocator_.capacity();
    const auto required_elements =
        std::bit_ceil(std::max(element_count, allocator_.capacity() * 2));
    const auto new_cap_elements = required_elements;
    ALWAYS_ASSERT(allocator_.grow(new_cap_elements - old_cap_elements));
    // TODO: is this line needed
    reserve_buffer_space(new_cap_elements);
    return allocate(element_count, resize_occured);
  }
  reserve_buffer_space(allocator_.capacity());
  allocated_element_count_ += element_count;
  max_seen_size_ = std::max<uint32_t>(max_seen_size_, alloc.offset + element_count);
  ASSERT(allocator_.allocationSize(alloc) >= element_count);
  return alloc;
}

BackedGPUAllocator::BackedGPUAllocator(BackedGPUAllocator&& other) noexcept
    : backing_buffer_(std::exchange(other.backing_buffer_, rhi::BufferHandleHolder{})),
      allocator_(std::move(other.allocator_)),
      bytes_per_element_(std::exchange(other.bytes_per_element_, 0)),
      device_(other.device_),
      buffer_copy_mgr_(other.buffer_copy_mgr_) {}

void BackedGPUAllocator::free(OffsetAllocator::Allocation alloc) {
  if (alloc.offset != OffsetAllocator::Allocation::NO_SPACE) {
    allocated_element_count_ -= allocator_.allocationSize(alloc);
    allocator_.free(alloc);
  }
}

void BackedGPUAllocator::reserve_buffer_space(uint32_t element_count, bool need_copy) {
  auto* buf = device_.get_buf(backing_buffer_);
  size_t required_size = element_count * bytes_per_element_;
  if (buf && buf->size() >= required_size) {
    return;
  }
  auto new_buffer_desc = buffer_desc_;
  new_buffer_desc.size = std::bit_ceil(required_size);
  auto new_buf_handle = device_.create_buf_h(new_buffer_desc);
  if (backing_buffer_.is_valid() && need_copy) {
    buffer_copy_mgr_.add_copy({
        .src_buf = backing_buffer_.handle,
        .dst_buf = new_buf_handle.handle,
        .size = buf->size(),
    });
  }
  backing_buffer_ = std::move(new_buf_handle);
}

}  // namespace gfx

}  // namespace TENG_NAMESPACE
