#pragma once

#include "gfx/GFXTypes.hpp"
#include "gfx/RendererTypes.hpp"  // IWYU pragma: keep

namespace rhi {

class CmdEncoder {
 public:
  virtual void begin_rendering(std::initializer_list<RenderingAttachmentInfo> attachments) = 0;
  virtual void bind_pipeline(rhi::PipelineHandle handle) = 0;
  CmdEncoder() = default;
  virtual ~CmdEncoder() = default;
  virtual void end_encoding() = 0;
  virtual void set_viewport(glm::uvec2 min, glm::uvec2 max) = 0;

 private:
};

}  // namespace rhi
