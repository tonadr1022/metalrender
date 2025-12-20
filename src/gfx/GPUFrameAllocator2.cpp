#include "GPUFrameAllocator2.hpp"

#include "core/Util.hpp"
#include "gfx/Buffer.hpp"
#include "gfx/Device.hpp"

namespace gfx {

GPUFrameAllocator2::GPUFrameAllocator2(size_t size, rhi::Device* device) {
  capacity_ = size;
  device_ = device;
  for (size_t i = 0; i < device_->get_info().frames_in_flight; i++) {
    buffers[i] = device->create_buf_h(rhi::BufferDesc{.storage_mode = rhi::StorageMode::CPUAndGPU,
                                                      .usage = rhi::BufferUsage_Storage,
                                                      .size = size,
                                                      .bindless = true});
  }
}

GPUFrameAllocator2::Alloc GPUFrameAllocator2::alloc(size_t size) {
  size = align_up(size, 16);
  ALWAYS_ASSERT(size + offset_ <= capacity_);
  size_t offset = offset_;
  offset_ += size;
  auto handle = buffers[frame_idx_].handle;
  return {handle, offset, (uint8_t*)device_->get_buf(handle)->contents() + offset};
}

GPUFrameAllocator2::Alloc GPUFrameAllocator2::alloc(size_t size, void* data) {
  auto result = alloc(size);
  memcpy(result.write_ptr, data, size);
  return result;
}

}  // namespace gfx
