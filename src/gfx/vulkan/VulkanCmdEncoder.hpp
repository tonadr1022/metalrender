#pragma once

#include <volk.h>

#include "gfx/CmdEncoder.hpp"

namespace gfx::vk {

class VulkanCmdEncoder : public rhi::CmdEncoder {
 public:
  explicit VulkanCmdEncoder(VkCommandBuffer cmd_buf, VkPipelineLayout shared_pipeline_layout)
      : cmd_buf_(cmd_buf), shared_pipeline_layout_(shared_pipeline_layout) {}
  void begin_rendering(std::initializer_list<rhi::RenderingAttachmentInfo> attachments) override;
  void bind_pipeline(rhi::PipelineHandle handle) override;
  void bind_pipeline(const rhi::PipelineHandleHolder& handle);

  void draw_primitives(rhi::PrimitiveTopology topology, size_t vertex_start, size_t count,
                       size_t instance_count) override;
  void draw_primitives(rhi::PrimitiveTopology topology, size_t vertex_start, size_t count) {
    draw_primitives(topology, vertex_start, count, 1);
  }
  void draw_primitives(rhi::PrimitiveTopology topology, size_t count) {
    draw_primitives(topology, 0, count, 1);
  }

  void draw_indexed_primitives(rhi::PrimitiveTopology topology, rhi::BufferHandle index_buf,
                               size_t index_start, size_t count, size_t instance_count,
                               size_t base_vertex, size_t base_instance) override;
  void draw_indexed_primitives(rhi::PrimitiveTopology topology, rhi::BufferHandle index_buf,
                               size_t index_start, size_t count) {
    draw_indexed_primitives(topology, index_buf, index_start, count, 1, 0, 0);
  }
  void set_depth_stencil_state(rhi::CompareOp depth_compare_op, bool depth_write_enabled) override;
  void set_wind_order(rhi::WindOrder wind_order) override;
  void set_cull_mode(rhi::CullMode cull_mode) override;

  void push_constants(void* data, size_t size) override;
  void end_encoding() override;
  void set_viewport(glm::uvec2 min, glm::uvec2 extent) override;

  void upload_texture_data(rhi::BufferHandle src_buf, size_t src_offset, size_t src_bytes_per_row,
                           rhi::TextureHandle dst_tex) override;
  void copy_tex_to_buf(rhi::TextureHandle src_tex, size_t src_slice, size_t src_level,
                       rhi::BufferHandle dst_buf, size_t dst_offset) override;
  //  void bind_index_buf(rhi::BufferHandle index_buf, size_t offset) override {}
  //  void bind_index_buf(rhi::BufferHandle index_buf) { bind_index_buf(index_buf, 0); }

  void prepare_indexed_indirect_draws(rhi::BufferHandle indirect_buf, size_t offset,
                                      size_t draw_cnt, rhi::BufferHandle index_buf,
                                      size_t index_buf_offset) override;

  void barrier(rhi::PipelineStage src_stage, rhi::AccessFlags src_access,
               rhi::PipelineStage dst_stage, rhi::AccessFlags dst_access) override;
  void draw_indexed_indirect(rhi::BufferHandle indirect_buf, size_t offset,
                             size_t draw_cnt) override;

 private:
  friend class VulkanDevice;
  VkCommandBuffer cmd_buf_;
  VkPipelineLayout shared_pipeline_layout_;
};

}  // namespace gfx::vk
