#include "BufferResize.hpp"

#include "core/Config.hpp"
#include "gfx/GPUFrameAllocator2.hpp"
#include "gfx/rhi/Buffer.hpp"
#include "gfx/rhi/Device.hpp"

namespace TENG_NAMESPACE {

namespace gfx {

void BufferCopyMgr::copy_to_buffer(const void* src_data, size_t src_size,
                                   rhi::BufferHandle dst_buffer, size_t dst_offset) {
  // if dst buffer is cpu visible, direct copy, otherwise copy to staging buffer
  // and enqueue staging -> dst buffer copy.
  auto* buf = device_->get_buf(dst_buffer);
  if (buf->is_cpu_visible()) {
    ASSERT(dst_offset + src_size <= buf->desc().size);
    memcpy((uint8_t*)buf->contents() + dst_offset, src_data, src_size);
  } else {
    auto upload_buf = staging_buffer_allocator_.alloc(src_size);
    memcpy((uint8_t*)upload_buf.write_ptr, src_data, src_size);
    add_copy({
        .src_buf = upload_buf.buf,
        .dst_buf = dst_buffer,
        .size = src_size,
        .src_offset = upload_buf.offset,
        .dst_offset = dst_offset,
    });
  }
}

void BufferCopyMgr::add_copy(const BufferCopy& copy) {
  // if both are CPU mapped, direct memcpy
  auto* src_buf = device_->get_buf(copy.src_buf);
  auto* dst_buf = device_->get_buf(copy.dst_buf);
  if (src_buf->is_cpu_visible() && dst_buf->is_cpu_visible()) {
    memcpy((uint8_t*)dst_buf->contents() + copy.dst_offset,
           (uint8_t*)src_buf->contents() + copy.src_offset, copy.size);
  } else {
    copies_.push_back(copy);
  }
}

}  // namespace gfx

}  // namespace TENG_NAMESPACE
