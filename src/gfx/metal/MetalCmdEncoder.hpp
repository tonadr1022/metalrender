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

  MetalDevice* device_{};
  MTL4::CommandBuffer* cmd_buf_{};
  MTL4::RenderCommandEncoder* curr_render_enc_{};
  MTL4::ComputeCommandEncoder* curr_compute_enc_{};
  MTL::ArgumentEncoder* top_level_arg_enc_{};
  MTL4::ArgumentTable* arg_table_{};
  MTL::Buffer* curr_arg_buf_{};
  size_t curr_arg_buf_offset_{};
};
