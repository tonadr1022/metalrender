#pragma once

#include "gfx/RenderGraph.hpp"
#include "gfx/rhi/Config.hpp"
#include "gfx/rhi/Device.hpp"

namespace gfx {

struct GPUFrameAllocator3;
class ShaderManager;

class ImGuiRenderer {
 public:
  explicit ImGuiRenderer(ShaderManager& shader_mgr, rhi::Device* device);
  void render(rhi::CmdEncoder* enc, glm::uvec2 fb_size, size_t frame_in_flight);
  void flush_pending_texture_uploads(rhi::CmdEncoder* enc,
                                     GPUFrameAllocator3& staging_buffer_allocator);
  rhi::PipelineHandleHolder pso_;

  std::vector<rhi::BufferHandleHolder> buffers_[k_max_frames_in_flight];
  rhi::BufferHandleHolder get_buffer_of_size(size_t size, size_t frame_in_flight,
                                             const char* name = "imgui_renderer_buf");
  void return_buffer(rhi::BufferHandleHolder&& handle, size_t frame_in_flight);
  bool has_dirty_textures();
  void add_dirty_textures_to_pass(RGPass& pass, bool read_access);

 private:
  rhi::Device* device_;
};

}  // namespace gfx
