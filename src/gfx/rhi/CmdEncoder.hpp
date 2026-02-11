#pragma once

#include "GFXTypes.hpp"
#include "core/Config.hpp"
#include "gfx/rhi/Barrier.hpp"

namespace TENG_NAMESPACE {

namespace rhi {

using AccessFlagsBits = uint64_t;

class CmdEncoder {
 public:
  virtual void set_debug_name(const char* name) = 0;
  virtual void begin_rendering(std::initializer_list<RenderingAttachmentInfo> attachments) = 0;
  virtual void end_rendering() = 0;
  // pipelines are compiled on demand if the provided pipeline doesn't have matching render target
  // info
  virtual void bind_pipeline(PipelineHandle handle) = 0;
  void bind_pipeline(const PipelineHandleHolder& handle) { bind_pipeline(handle.handle); }

  virtual void draw_primitives(PrimitiveTopology topology, size_t vertex_start, size_t count,
                               size_t instance_count) = 0;
  void draw_primitives(PrimitiveTopology topology, size_t vertex_start, size_t count) {
    draw_primitives(topology, vertex_start, count, 1);
  }
  void draw_primitives(PrimitiveTopology topology, size_t count) {
    draw_primitives(topology, 0, count, 1);
  }

  virtual void draw_indexed_primitives(PrimitiveTopology topology, BufferHandle index_buf,
                                       size_t index_start, size_t count, size_t instance_count,
                                       size_t base_vertex_idx, size_t base_instance,
                                       IndexType index_type) = 0;
  void draw_indexed_primitives(PrimitiveTopology topology, BufferHandle index_buf,
                               size_t index_start, size_t count, IndexType index_type) {
    draw_indexed_primitives(topology, index_buf, index_start, count, 1, 0, 0, index_type);
  }
  virtual void set_depth_stencil_state(CompareOp depth_compare_op, bool depth_write_enabled) = 0;
  virtual void set_wind_order(WindOrder wind_order) = 0;
  virtual void set_cull_mode(CullMode cull_mode) = 0;

  virtual void push_constants(void* data, size_t size) = 0;
  CmdEncoder() = default;
  virtual ~CmdEncoder() = default;
  virtual void end_encoding() = 0;
  virtual void set_label(const std::string& label) = 0;
  virtual void set_viewport(glm::uvec2 min, glm::uvec2 extent) = 0;
  virtual void set_scissor(glm::uvec2 min, glm::uvec2 extent) = 0;

  virtual void upload_texture_data(rhi::BufferHandle src_buf, size_t src_offset,
                                   size_t src_bytes_per_row, rhi::TextureHandle dst_tex) = 0;
  virtual void upload_texture_data(rhi::BufferHandle src_buf, size_t src_offset,
                                   size_t src_bytes_per_row, rhi::TextureHandle dst_tex,
                                   glm::uvec3 src_size, glm::uvec3 dst_origin, int mip_level) = 0;
  virtual void copy_tex_to_buf(rhi::TextureHandle src_tex, size_t src_slice, size_t src_level,
                               rhi::BufferHandle dst_buf, size_t dst_offset) = 0;
  virtual void copy_buffer_to_buffer(rhi::BufferHandle src_buf, size_t src_offset,
                                     rhi::BufferHandle dst_buf, size_t dst_offset, size_t size) = 0;

  [[nodiscard]] virtual uint32_t prepare_indexed_indirect_draws(
      rhi::BufferHandle indirect_buf, size_t offset, size_t tot_draw_cnt,
      rhi::BufferHandle index_buf, size_t index_buf_offset, void* push_constant_data,
      size_t push_constant_size, size_t vertex_stride) = 0;

  virtual void barrier(PipelineStage src_stage, AccessFlags src_access, PipelineStage dst_stage,
                       AccessFlags dst_access) = 0;
  virtual void barrier(rhi::BufferHandle buf, rhi::PipelineStage src_stage,
                       rhi::AccessFlags src_access, rhi::PipelineStage dst_stage,
                       rhi::AccessFlags dst_access) = 0;
  virtual void barrier(rhi::TextureHandle buf, rhi::PipelineStage src_stage,
                       rhi::AccessFlags src_access, rhi::PipelineStage dst_stage,
                       rhi::AccessFlags dst_access) = 0;
  virtual void barrier(rhi::BufferHandle buf, rhi::PipelineStage src_stage,
                       rhi::AccessFlags src_access, rhi::PipelineStage dst_stage,
                       rhi::AccessFlags dst_access, size_t offset, size_t size) = 0;
  virtual void barrier(GPUBarrier* gpu_barrier, size_t barrier_count) = 0;
  virtual void barrier(GPUBarrier* gpu_barrier) { barrier(gpu_barrier, 1); }
  virtual void draw_indexed_indirect(rhi::BufferHandle indirect_buf, uint32_t indirect_buf_id,
                                     size_t draw_cnt, size_t offset_i) = 0;
  virtual void draw_mesh_threadgroups(glm::uvec3 thread_groups,
                                      glm::uvec3 threads_per_task_thread_group,
                                      glm::uvec3 threads_per_mesh_thread_group) = 0;
  // This only supports length 1 right now.
  virtual void draw_mesh_threadgroups_indirect(rhi::BufferHandle indirect_buf,
                                               size_t indirect_buf_offset,
                                               glm::uvec3 threads_per_task_thread_group,
                                               glm::uvec3 threads_per_mesh_thread_group) = 0;
  virtual void dispatch_compute(glm::uvec3 thread_groups, glm::uvec3 threads_per_threadgroup) = 0;
  virtual void fill_buffer(rhi::BufferHandle handle, uint32_t offset_bytes, uint32_t size,
                           uint32_t value) = 0;
  virtual void push_debug_group(const char* name) = 0;
  virtual void pop_debug_group() = 0;

  virtual void bind_srv(rhi::TextureHandle texture, uint32_t slot) { bind_srv(texture, slot, -1); }
  virtual void bind_srv(rhi::TextureHandle texture, uint32_t slot, int subresource_id) = 0;
  virtual void bind_srv(rhi::BufferHandle buffer, uint32_t slot) { bind_srv(buffer, slot, 0); }
  virtual void bind_srv(rhi::BufferHandle buffer, uint32_t slot, size_t offset_bytes) = 0;

  virtual void bind_uav(rhi::TextureHandle texture, uint32_t slot) { bind_uav(texture, slot, -1); }
  virtual void bind_uav(rhi::TextureHandle texture, uint32_t slot, int subresource_id) = 0;
  virtual void bind_uav(rhi::BufferHandle buffer, uint32_t slot) { bind_uav(buffer, slot, 0); }
  virtual void bind_uav(rhi::BufferHandle buffer, uint32_t slot, size_t offset_bytes) = 0;

  virtual void bind_cbv(rhi::BufferHandle buffer, uint32_t slot, size_t offset_bytes) = 0;

  virtual void write_timestamp(rhi::QueryPoolHandle query_pool, uint32_t query_index) = 0;
  virtual void query_resolve(rhi::QueryPoolHandle query_pool, uint32_t start_query,
                             uint32_t query_count, rhi::BufferHandle dst_buffer,
                             size_t dst_offset) = 0;
};

}  // namespace rhi

}  // namespace TENG_NAMESPACE
