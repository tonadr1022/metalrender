#pragma once

#include "gfx/Config.hpp"
#include "gfx/RendererTypes.hpp"
namespace gfx {

struct GPUFrameAllocator2 {
  struct Alloc {
    rhi::BufferHandle buf;
    size_t offset;
    void* write_ptr;
  };

  GPUFrameAllocator2(size_t size, rhi::Device* device);

  Alloc alloc(size_t size);
  Alloc alloc(size_t size, void* data);

  void reset(size_t frame_idx) {
    frame_idx_ = frame_idx;
    offset_ = 0;
  }

  std::array<rhi::BufferHandleHolder, k_max_frames_in_flight> buffers;
  size_t capacity_{};
  size_t offset_{};
  size_t frame_idx_{};
  rhi::Device* device_;
};

}  // namespace gfx
