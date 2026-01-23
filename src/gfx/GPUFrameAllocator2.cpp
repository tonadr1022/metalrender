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

GPUFrameAllocator3::GPUFrameAllocator3(rhi::Device* device) : device_(device) {
  device_ = device;
  for (uint32_t i = 0; i < device_->get_info().frames_in_flight; i++) {
    frames_[i].free_staging_buffers.reserve(5);
    frames_[i].free_staging_buffers.emplace_back(create_staging_buffer(16u * 1024 * 1024));
  }
}

GPUFrameAllocator3::Alloc GPUFrameAllocator3::alloc(uint32_t size, void* data) {
  auto allocation = alloc(size);
  memcpy(allocation.write_ptr, data, size);
  return allocation;
}

GPUFrameAllocator3::Alloc GPUFrameAllocator3::alloc(uint32_t size) {
  auto& frame = curr_frame();
  size = align_up(size, k_alignment);
  StagingBuffer* selected_buf = nullptr;
  uint32_t selected_buf_idx = UINT32_MAX;
  for (uint32_t i = 0; i < static_cast<uint32_t>(frame.free_staging_buffers.size()); i++) {
    auto& staging_buf = frame.free_staging_buffers[i];
    if (staging_buf.get_remaining_size() >= size) {
      selected_buf = &staging_buf;
      selected_buf_idx = i;
      break;
    }
  }

  constexpr auto k_min_staging_buf_capacity = 16u * 1024 * 1024;
  if (!selected_buf) {
    auto capacity = std::max(size, k_min_staging_buf_capacity);
    if (size > k_min_staging_buf_capacity) {
      capacity = align_up(size, k_min_staging_buf_capacity);
    }
    selected_buf_idx = static_cast<uint32_t>(frame.free_staging_buffers.size());
    frame.free_staging_buffers.emplace_back(create_staging_buffer(capacity));
    selected_buf = &frame.free_staging_buffers.back();
  }

  uint32_t offset = selected_buf->curr_offset;
  selected_buf->curr_offset += size;
  auto* buf = device_->get_buf(selected_buf->buffer);
  Alloc allocation{selected_buf->buffer.handle, offset, (uint8_t*)buf->contents() + offset};
  if (selected_buf->get_remaining_size() == 0) {
    frame.full_staging_buffers.emplace_back(std::move(*selected_buf));
    frame.free_staging_buffers.erase(frame.free_staging_buffers.begin() + selected_buf_idx);
  }
  return allocation;
}

void GPUFrameAllocator3::reset(uint32_t frame_idx) {
  frame_idx_ = frame_idx;
  for (auto& buf : curr_frame().full_staging_buffers) {
    buf.curr_offset = 0;
    curr_frame().free_staging_buffers.emplace_back(std::move(buf));
  }
}

GPUFrameAllocator3::StagingBuffer GPUFrameAllocator3::create_staging_buffer(uint32_t size) {
  return {device_->create_buf_h({
              .storage_mode = rhi::StorageMode::CPUAndGPU,
              .usage = rhi::BufferUsage_Storage,
              .size = size,
              .bindless = true,
              .name = "gpu_frame_allocator3_staging_buf",
          }),
          0, size};
}

}  // namespace gfx
