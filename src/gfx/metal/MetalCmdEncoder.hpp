#pragma once

#include "gfx/CmdEncoder.hpp"

class MetalDevice;

namespace MTL4 {

class CommandBuffer;
class RenderCommandEncoder;
class ComputeCommandEncoder;

}  // namespace MTL4

class MetalCmdEncoder : public rhi::CmdEncoder {
 public:
  MetalCmdEncoder() = default;
  explicit MetalCmdEncoder(MTL4::CommandBuffer* cmd_buf);
  void begin_rendering(std::initializer_list<rhi::RenderingAttachmentInfo> attachments) override;
  void end_encoding() override;
  void bind_pipeline(rhi::PipelineHandle handle) override;

 private:
  MetalDevice* device_{};
  MTL4::CommandBuffer* cmd_buf_{};
  MTL4::RenderCommandEncoder* curr_render_enc_{};
  MTL4::ComputeCommandEncoder* curr_compute_enc_{};
};
