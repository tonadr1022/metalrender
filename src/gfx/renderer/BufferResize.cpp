#include "BufferResize.hpp"

#include "gfx/Buffer.hpp"
#include "gfx/Device.hpp"
#include "gfx/GPUFrameAllocator2.hpp"

void gfx::BufferCopyMgr::copy_to_buffer(const void* src_data, size_t src_size,
                                        rhi::BufferHandle dst_buffer, size_t dst_offset) {
  // if dst buffer is cpu visible, direct copy, otherwise copy to staging buffer
  // and enqueue staging -> dst buffer copy.
  auto* buf = device_->get_buf(dst_buffer);
  if (buf->is_cpu_visible()) {
    memcpy((uint8_t*)buf->contents() + dst_offset, src_data, src_size);
  } else {
    auto upload_buf = staging_buffer_allocator_.alloc(src_size);
    memcpy((uint8_t*)upload_buf.write_ptr, src_data, src_size);
    add_copy(upload_buf.buf, upload_buf.offset, dst_buffer, dst_offset, src_size);
  }
}
