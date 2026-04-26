#include "InstanceMgr.hpp"

#include "gfx/MemeRenderer123.hpp"

namespace TENG_NAMESPACE {

namespace gfx {

namespace {

uint32_t next_pow2(uint32_t val) {
  uint32_t v = 1;
  while (v < val) {
    v *= 2;
  }
  return v;
}

}  // namespace

void InstanceMgr::free(const Alloc& alloc, uint32_t frame_in_flight) {
  pending_frees_[frame_in_flight].push_back(alloc);
}

void InstanceMgr::flush_pending_frees(uint32_t curr_frame_in_flight, rhi::CmdEncoder* enc) {
  for (const auto& allocs : pending_frees_[curr_frame_in_flight]) {
    const auto& alloc = allocs.instance_data_alloc;
    auto element_count = allocator_.allocationSize(alloc);
    enc->fill_buffer(instance_data_buf_.handle, alloc.offset * sizeof(InstanceData),
                     element_count * sizeof(InstanceData), 0xFFFFFFFF);
    if (need_cpu_draws_) {
      for (size_t i = 0; i < element_count; i++) {
        cpu_draw_cmds()[alloc.offset + i].instance_count = 0;
      }
    }
    if (!mesh_shaders_enabled_) {
      enc->fill_buffer(draw_cmd_buf_.handle, alloc.offset * sizeof(IndexedIndirectDrawCmd),
                       element_count * sizeof(IndexedIndirectDrawCmd), 0xFFFFFFFF);
    }
    allocator_.free(alloc);
    curr_element_count_ -= element_count;

    if (mesh_shaders_enabled_) {
      meshlet_vis_buf_allocator_.free(allocs.meshlet_vis_alloc);
    }
  }
  pending_frees_[curr_frame_in_flight].clear();
}

bool InstanceMgr::ensure_buffer_space(size_t element_count) {
  bool resized{};
  if (element_count == 0) return resized;
  if (!instance_data_buf_.is_valid() ||
      device_.get_buf(instance_data_buf_)->size() < element_count * sizeof(InstanceData)) {
    auto old_buf = std::move(instance_data_buf_);
    auto new_buf = device_.create_buf_h({
        .usage = rhi::BufferUsage::Storage,
        .size = sizeof(InstanceData) * element_count,
        // Don't enable CPU access for copying since this copy should be delayed until a copy
        // happens on the GPU timeline, otherwise artifacts can appear when the buffer is updated
        // (objects added/removed).
        .flags = rhi::BufferDescFlags::DisableCPUAccessOnUMA,
        .name = "intance_data_buf",
    });

    if (old_buf.is_valid()) {
      buffer_copy_mgr_.add_copy(old_buf.handle, 0, new_buf.handle, 0,
                                device_.get_buf(old_buf)->size(),
                                rhi::PipelineStage::ComputeShader | rhi::PipelineStage::AllGraphics,
                                rhi::AccessFlags::ShaderRead);
      // Keep the old buffer alive until queued migration copies are submitted.
      buffer_copy_mgr_.defer_release(std::move(old_buf));
      resized = true;
    }
    instance_data_buf_ = std::move(new_buf);
  }

  if (!mesh_shaders_enabled_) {
    if (!draw_cmd_buf_.is_valid() ||
        device_.get_buf(draw_cmd_buf_)->size() < element_count * sizeof(IndexedIndirectDrawCmd)) {
      auto new_buf = device_.create_buf_h(rhi::BufferDesc{
          .usage = rhi::BufferUsage::Indirect,
          .size = sizeof(IndexedIndirectDrawCmd) * element_count,
          .flags = rhi::BufferDescFlags::CPUAccessible,
          .name = "draw_indexed_indirect_cmd_buf",
      });
      if (draw_cmd_buf_.is_valid()) {
        buffer_copy_mgr_.copy_to_buffer(
            device_.get_buf(draw_cmd_buf_)->contents(), device_.get_buf(draw_cmd_buf_)->size(),
            new_buf.handle, 0, rhi::PipelineStage::ComputeShader | rhi::PipelineStage::DrawIndirect,
            rhi::AccessFlags::IndirectCommandRead);
      }
      draw_cmd_buf_ = std::move(new_buf);
    }
  }
  return resized;
}
InstanceMgr::Alloc InstanceMgr::allocate(uint32_t element_count, uint32_t meshlet_instance_count) {
  OffsetAllocator::Allocation meshlet_vis_buf_alloc{};
  if (mesh_shaders_enabled_) {
    meshlet_vis_buf_alloc = meshlet_vis_buf_allocator_.allocate(meshlet_instance_count);
    if (meshlet_vis_buf_alloc.offset == OffsetAllocator::Allocation::NO_SPACE) {
      meshlet_vis_buf_allocator_.grow(
          std::max(meshlet_vis_buf_allocator_.capacity(), next_pow2(meshlet_instance_count)));
      meshlet_vis_buf_alloc = meshlet_vis_buf_allocator_.allocate(meshlet_instance_count);
      ASSERT(meshlet_vis_buf_alloc.offset != OffsetAllocator::Allocation::NO_SPACE);
    }
    stats_.max_seen_meshlet_instance_count =
        std::max(stats_.max_seen_meshlet_instance_count,
                 meshlet_vis_buf_alloc.offset + meshlet_instance_count);
  }
  return {.instance_data_alloc = allocate_instance_data(element_count),
          .meshlet_vis_alloc = meshlet_vis_buf_alloc};
}

[[nodiscard]] bool InstanceMgr::has_pending_frees(uint32_t curr_frame_in_flight) const {
  return !pending_frees_[curr_frame_in_flight].empty();
}

InstanceMgr::InstanceMgr(rhi::Device& device, BufferCopyMgr& buffer_copy_mgr,
                         uint32_t frames_in_flight, bool mesh_shaders_enabled)
    : allocator_(0),
      meshlet_vis_buf_allocator_(0),
      buffer_copy_mgr_(buffer_copy_mgr),
      frames_in_flight_(frames_in_flight),
      device_(device),
      mesh_shaders_enabled_(mesh_shaders_enabled) {}

OffsetAllocator::Allocation InstanceMgr::allocate_instance_data(uint32_t element_count) {
  if (element_count == 0) {
    return {};
  }
  OffsetAllocator::Allocation alloc = allocator_.allocate(element_count);
  while (alloc.offset == OffsetAllocator::Allocation::NO_SPACE) {
    const auto curr_capacity = static_cast<uint32_t>(allocator_.capacity());
    ensure_instance_capacity(curr_capacity + element_count);
    alloc = allocator_.allocate(element_count);
  }

  ensure_buffer_space(allocator_.capacity());
  curr_element_count_ += element_count;
  stats_.max_instance_data_count =
      std::max<uint32_t>(stats_.max_instance_data_count, alloc.offset + element_count);
  return alloc;
}

void InstanceMgr::reserve_space(uint32_t instance_data_count) {
  if (instance_data_count == 0) {
    return;
  }
  ensure_instance_capacity(curr_element_count_ + instance_data_count);
}

void InstanceMgr::ensure_instance_capacity(uint32_t min_capacity) {
  const auto old_capacity = static_cast<uint32_t>(allocator_.capacity());
  if (old_capacity < min_capacity) {
    const uint32_t doubled_capacity = old_capacity == 0 ? min_capacity : old_capacity * 2;
    const uint32_t target_capacity = std::max(next_pow2(min_capacity), doubled_capacity);
    allocator_.grow(target_capacity - old_capacity);
  }
  ensure_buffer_space(allocator_.capacity());
}

}  // namespace gfx
}  // namespace TENG_NAMESPACE