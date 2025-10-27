#pragma once

#include "gfx/RendererTypes.hpp"

namespace rhi {

class CmdEncoder {
 public:
  virtual void begin_rendering(std::initializer_list<RenderingAttachmentInfo> attachments) = 0;

 private:
};

}  // namespace rhi
