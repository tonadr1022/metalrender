#pragma once

#include "core/Config.hpp"
#include "gfx/rhi/GFXTypes.hpp"

namespace TENG_NAMESPACE {

namespace gfx {

struct GPUFrameAllocator3;

struct BufferCopy {
  rhi::BufferHandle src_buf;
  rhi::BufferHandle dst_buf;
  size_t size;
  size_t src_offset;
  size_t dst_offset;
  rhi::PipelineStage dst_stage;
  rhi::AccessFlags dst_access;
};

// TODO:  handle multiple resizes per frame
struct BufferCopyMgr {
  explicit BufferCopyMgr(rhi::Device* device, GPUFrameAllocator3& staging_buffer_allocator)
      : staging_buffer_allocator_(staging_buffer_allocator), device_(device) {}
  // void add_copy(const BufferCopy& copy);
  void add_copy(rhi::BufferHandle src_buf, size_t src_offset, rhi::BufferHandle dst_buf,
                size_t dst_offset, size_t size, rhi::PipelineStage dst_stage,
                rhi::AccessFlags dst_access);
  void enqueue_fill_buffer() {}

  void copy_to_buffer(const void* src_data, size_t src_size, rhi::BufferHandle dst_buffer,
                      size_t dst_offset, rhi::PipelineStage dst_stage, rhi::AccessFlags dst_access);
  void clear_copies() { copies_.clear(); }
  [[nodiscard]] const std::vector<BufferCopy>& get_copies() const { return copies_; }

 private:
  std::vector<BufferCopy> copies_;
  GPUFrameAllocator3& staging_buffer_allocator_;
  rhi::Device* device_{nullptr};
};

}  // namespace gfx

}  // namespace TENG_NAMESPACE
