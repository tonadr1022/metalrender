#pragma once

#include "core/Config.hpp"
#include "gfx/renderer/BufferResize.hpp"
#include "gfx/rhi/Config.hpp"
#include "hlsl/shared_indirect.h"
#include "offsetAllocator.hpp"

namespace TENG_NAMESPACE {

namespace gfx {

namespace rhi {
class Device;
class CmdEncoder;

}  // namespace rhi

class MemeRenderer123;

class InstanceMgr {
 public:
  InstanceMgr(const InstanceMgr&) = delete;
  InstanceMgr(InstanceMgr&&) = delete;
  InstanceMgr& operator=(const InstanceMgr&) = delete;
  InstanceMgr& operator=(InstanceMgr&&) = delete;
  struct Alloc {
    OffsetAllocator::Allocation instance_data_alloc;
    OffsetAllocator::Allocation meshlet_vis_alloc;
  };

  InstanceMgr(rhi::Device& device, BufferCopyMgr& buffer_copy_mgr, uint32_t frames_in_flight,
              MemeRenderer123& renderer);
  [[nodiscard]] bool has_draws() const { return curr_element_count_ > 0; }
  Alloc allocate(uint32_t element_count, uint32_t meshlet_instance_count);

  [[nodiscard]] size_t allocation_size(OffsetAllocator::Allocation alloc) const {
    return allocator_.allocationSize(alloc);
  }

  void free(const Alloc& alloc, uint32_t frame_in_flight);
  void flush_pending_frees(uint32_t curr_frame_in_flight, rhi::CmdEncoder* enc);
  [[nodiscard]] bool has_pending_frees(uint32_t curr_frame_in_flight) const;
  void zero_out_freed_instances(rhi::CmdEncoder* enc);
  [[nodiscard]] rhi::BufferHandle get_instance_data_buf() const {
    return instance_data_buf_.handle;
  }
  [[nodiscard]] size_t get_num_meshlet_vis_buf_elements() const {
    return meshlet_vis_buf_allocator_.capacity();
  }
  std::array<std::vector<Alloc>, k_max_frames_in_flight> pending_frees_;
  [[nodiscard]] rhi::BufferHandle get_draw_cmd_buf() const { return draw_cmd_buf_.handle; }

  struct Stats {
    uint32_t max_instance_data_count;
    uint32_t max_seen_meshlet_instance_count;
  };

  [[nodiscard]] const Stats& stats() const { return stats_; }

  void reserve_space(uint32_t instance_data_count);
  [[nodiscard]] std::vector<IndexedIndirectDrawCmd>& cpu_draw_cmds() { return cpu_draw_cmds_; }
  [[nodiscard]] bool need_draw_cmds_on_cpu() const { return need_cpu_draws_; }

 private:
  OffsetAllocator::Allocation allocate_instance_data(uint32_t element_count);
  // returns true if resize occured
  std::vector<IndexedIndirectDrawCmd> cpu_draw_cmds_;
  bool need_cpu_draws_{true};
  bool ensure_buffer_space(size_t element_count);
  OffsetAllocator::Allocator allocator_;
  rhi::BufferHandleHolder instance_data_buf_;
  rhi::BufferHandleHolder draw_cmd_buf_;
  OffsetAllocator::Allocator meshlet_vis_buf_allocator_;
  BufferCopyMgr& buffer_copy_mgr_;
  Stats stats_{};
  uint32_t curr_element_count_{};
  uint32_t frames_in_flight_{};
  rhi::Device& device_;
  MemeRenderer123& renderer_;
};
}  // namespace gfx

}  // namespace TENG_NAMESPACE