#pragma once

#include "gfx/GFXTypes.hpp"
#include "gfx/RendererTypes.hpp"  // IWYU pragma: keep

namespace rhi {

class CmdEncoder {
 public:
  virtual void begin_rendering(std::initializer_list<RenderingAttachmentInfo> attachments) = 0;
  virtual void bind_pipeline(rhi::PipelineHandle handle) = 0;
  void bind_pipeline(const rhi::PipelineHandleHolder& handle) { bind_pipeline(handle.handle); }

  virtual void draw_primitives(rhi::PrimitiveTopology topology, size_t vertex_start, size_t count,
                               size_t instance_count) = 0;
  void draw_primitives(rhi::PrimitiveTopology topology, size_t vertex_start, size_t count) {
    draw_primitives(topology, vertex_start, count, 1);
  }
  void draw_primitives(rhi::PrimitiveTopology topology, size_t count) {
    draw_primitives(topology, 0, count, 1);
  }

  virtual void draw_indexed_primitives(rhi::PrimitiveTopology topology, rhi::BufferHandle index_buf,
                                       size_t index_start, size_t count) = 0;

  virtual void push_constants(void* data, size_t size) = 0;
  CmdEncoder() = default;
  virtual ~CmdEncoder() = default;
  virtual void end_encoding() = 0;
  virtual void set_viewport(glm::uvec2 min, glm::uvec2 max) = 0;

 private:
};

}  // namespace rhi
