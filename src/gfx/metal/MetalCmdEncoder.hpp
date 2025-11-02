#pragma once

#include "gfx/CmdEncoder.hpp"

class MetalDevice;

namespace MTL4 {

class CommandBuffer;
class RenderCommandEncoder;
class ComputeCommandEncoder;
class ArgumentTable;

}  // namespace MTL4

namespace MTL {

class ArgumentEncoder;
class Buffer;

}  // namespace MTL

class MetalCmdEncoder : public rhi::CmdEncoder {
 public:
  MetalCmdEncoder() = default;
  MetalCmdEncoder(MetalDevice* device, MTL4::CommandBuffer* cmd_buf,
                  MTL::ArgumentEncoder* top_level_arg_enc);

  void begin_rendering(std::initializer_list<rhi::RenderingAttachmentInfo> attachments) override;
  void end_encoding() override;
  void bind_pipeline(rhi::PipelineHandle handle) override;
  void set_viewport(glm::uvec2 min, glm::uvec2 max) override;
  void draw_primitives(rhi::PrimitiveTopology topology, size_t vertex_start, size_t count,
                       size_t instance_count) override;
  void push_constants(void* data, size_t size) override;
  void draw_indexed_primitives(rhi::PrimitiveTopology topology, rhi::BufferHandle index_buf,
                               size_t index_start, size_t count) override;
  void set_depth_stencil_state(rhi::CompareOp depth_compare_op, bool depth_write_enabled) override;
  void set_wind_order(rhi::WindOrder wind_order) override;
  void set_cull_mode(rhi::CullMode cull_mode) override;
  // TODO: will this work with vulkanisms
  void copy_buf_to_tex(rhi::BufferHandle src_buf, size_t src_offset, size_t src_bytes_per_row,
                       rhi::TextureHandle dst_tex) override;

  MetalDevice* device_{};
  MTL4::CommandBuffer* cmd_buf_{};
  MTL4::RenderCommandEncoder* render_enc_{};
  MTL4::ComputeCommandEncoder* compute_enc_{};
  MTL::ArgumentEncoder* top_level_arg_enc_{};
  MTL4::ArgumentTable* arg_table_{};
  MTL::Buffer* curr_arg_buf_{};
  size_t curr_arg_buf_offset_{};

 private:
  void end_render_encoder();
  void end_compute_encoder();
};
