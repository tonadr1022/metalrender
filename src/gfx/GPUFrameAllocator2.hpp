#pragma once

#include "gfx/Config.hpp"
#include "gfx/rhi/GFXTypes.hpp"

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

struct GPUFrameAllocator3 {
  struct Alloc {
    rhi::BufferHandle buf;
    uint32_t offset;
    void* write_ptr;
  };

  explicit GPUFrameAllocator3(rhi::Device* device);

  Alloc alloc(uint32_t size);
  Alloc alloc(uint32_t size, void* data);

  void reset(uint32_t frame_idx);

  struct StagingBuffer {
    rhi::BufferHandleHolder buffer;
    uint32_t curr_offset;
    uint32_t capacity;
    [[nodiscard]] uint32_t get_remaining_size() const { return capacity - curr_offset; }
  };

  struct PerFrame {
    std::vector<StagingBuffer> free_staging_buffers;
    std::vector<StagingBuffer> full_staging_buffers;
  };

 private:
  std::array<PerFrame, k_max_frames_in_flight> frames_;
  StagingBuffer create_staging_buffer(uint32_t size);
  PerFrame& curr_frame() { return frames_[frame_idx_]; }
  std::array<rhi::BufferHandleHolder, k_max_frames_in_flight> buffers;
  uint32_t offset_{};
  uint32_t frame_idx_{};
  rhi::Device* device_;
  constexpr static uint32_t k_alignment = 16;
};

}  // namespace gfx
