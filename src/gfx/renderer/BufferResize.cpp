#include "BufferResize.hpp"

#include <cstring>

#include "core/Config.hpp"
#include "gfx/GPUFrameAllocator2.hpp"
#include "gfx/rhi/Buffer.hpp"
#include "gfx/rhi/Device.hpp"

namespace TENG_NAMESPACE {

namespace gfx {

void BufferCopyMgr::copy_to_buffer(const void* src_data, size_t src_size,
                                   rhi::BufferHandle dst_buffer, size_t dst_offset,
                                   rhi::PipelineStage dst_stage, rhi::AccessFlags dst_access) {
  // if dst buffer is cpu visible, direct copy, otherwise copy to staging buffer
  // and enqueue staging -> dst buffer copy.
  auto* buf = device_->get_buf(dst_buffer);
  if (buf->is_cpu_visible()) {
    ASSERT(dst_offset + src_size <= buf->desc().size);
    memcpy((uint8_t*)buf->contents() + dst_offset, src_data, src_size);
  } else {
    auto upload_buf = staging_buffer_allocator_.alloc(src_size);
    memcpy((uint8_t*)upload_buf.write_ptr, src_data, src_size);
    ASSERT(dst_offset + src_size <= buf->desc().size);
    add_copy(upload_buf.buf, upload_buf.offset, dst_buffer, dst_offset, src_size, dst_stage,
             dst_access);
  }
}

void BufferCopyMgr::add_copy(rhi::BufferHandle src_buf, size_t src_offset,
                             rhi::BufferHandle dst_buf, size_t dst_offset, size_t size,
                             rhi::PipelineStage dst_stage, rhi::AccessFlags dst_access) {
  // if both are CPU mapped, direct memcpy
  auto* src_b = device_->get_buf(src_buf);
  auto* dst_b = device_->get_buf(dst_buf);
  if (src_b->is_cpu_visible() && dst_b->is_cpu_visible()) {
    memcpy((uint8_t*)dst_b->contents() + dst_offset, (uint8_t*)src_b->contents() + src_offset,
           size);
  } else {
    copies_.push_back(BufferCopy{
        .src_buf = src_buf,
        .dst_buf = dst_buf,
        .size = size,
        .src_offset = src_offset,
        .dst_offset = dst_offset,
        .dst_stage = dst_stage,
        .dst_access = dst_access,
    });
  }
}

}  // namespace gfx

}  // namespace TENG_NAMESPACE
