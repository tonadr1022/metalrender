#pragma once

#include "core/Config.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/rhi/Config.hpp"
#include "gfx/rhi/Device.hpp"
#include "imgui.h"

struct ImTextureData;

namespace TENG_NAMESPACE {

namespace gfx {

// Packed ImTextureRef / ImTextureID for ImGui::Image: bindless index of a single-mip
// Texture2D<float> view (see Device::get_tex_view_bindless_idx). Decoded in ImGuiRenderer::render.
constexpr uint64_t kImGuiTexRefBindlessFloatViewMagic = 0xA11CE000000000ull;
// CSM: low 32 = default bindless for shadow depth2DArray, high 32 = (0x0A11CEB << 8) | cascade.
constexpr uint32_t kImGuiCsmArrayHighPrefix = 0x0A11CEB;  // (tid>>40) == 0x0A11CEB in decode

[[nodiscard]] inline ImTextureRef MakeImGuiTexRefBindlessFloatView(uint32_t bindless_view_idx) {
  const uint64_t packed =
      kImGuiTexRefBindlessFloatViewMagic | static_cast<uint64_t>(bindless_view_idx);
  return ImTextureRef{static_cast<ImTextureID>(packed)};
}

// Sampled as Texture2D<float4> via bindless_textures (ImGuiRenderer decodes handle -> bindless_idx).
[[nodiscard]] inline ImTextureRef MakeImGuiTexRefTextureHandle(rhi::TextureHandle handle) {
  return ImTextureRef{static_cast<ImTextureID>(handle.to64())};
}

[[nodiscard]] inline ImTextureRef MakeImGuiTexRefCsmArraySlice(uint32_t array_bindless_idx,
                                                               uint32_t cascade_layer) {
  const uint32_t high32 = (kImGuiCsmArrayHighPrefix << 8) | (cascade_layer & 0xFFu);
  const uint64_t packed =
      (static_cast<uint64_t>(high32) << 32) | static_cast<uint64_t>(array_bindless_idx);
  return ImTextureRef{static_cast<ImTextureID>(packed)};
}

struct GPUFrameAllocator3;
class ShaderManager;

// custom ImGui backend. Probably not fully conformant but oh well it works.
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
  void shutdown();
  void destroy_texture(ImTextureData* im_tex_id);

 private:
  rhi::Device* device_;
};

}  // namespace gfx

}  // namespace TENG_NAMESPACE
