#pragma once

#include "gfx/GFXTypes.hpp"
#include "gfx/RendererTypes.hpp"  // IWYU pragma: keep

namespace rhi {

class CmdEncoder {
 public:
  virtual void begin_rendering(std::initializer_list<RenderingAttachmentInfo> attachments) = 0;
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
                                       size_t index_start, size_t count) = 0;
  virtual void set_depth_stencil_state(CompareOp depth_compare_op, bool depth_write_enabled) = 0;
  virtual void set_wind_order(WindOrder wind_order) = 0;
  virtual void set_cull_mode(CullMode cull_mode) = 0;

  virtual void push_constants(void* data, size_t size) = 0;
  CmdEncoder() = default;
  virtual ~CmdEncoder() = default;
  virtual void end_encoding() = 0;
  virtual void set_viewport(glm::uvec2 min, glm::uvec2 max) = 0;

  virtual void copy_buf_to_tex(rhi::BufferHandle src_buf, size_t src_offset,
                               size_t src_bytes_per_row, rhi::TextureHandle dst_tex) = 0;

 private:
};

}  // namespace rhi
