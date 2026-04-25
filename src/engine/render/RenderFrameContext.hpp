#pragma once

#include <cstdint>
#include <filesystem>
#include <glm/ext/vector_uint2.hpp>

namespace teng::gfx {
class ImGuiRenderer;
class ModelGPUMgr;
class RenderGraph;
class ShaderManager;
struct BufferCopyMgr;
struct GPUFrameAllocator3;

namespace rhi {
class Device;
class Swapchain;
}  // namespace rhi
}  // namespace teng::gfx

namespace teng {
class Window;
}

namespace teng::engine {

struct EngineTime;

struct RenderFrameContext {
  gfx::rhi::Device* device{};
  gfx::rhi::Swapchain* swapchain{};
  Window* window{};
  gfx::RenderGraph* render_graph{};
  gfx::ShaderManager* shader_mgr{};
  gfx::BufferCopyMgr* buffer_copy{};
  gfx::GPUFrameAllocator3* frame_staging{};
  gfx::ImGuiRenderer* imgui_renderer{};
  gfx::ModelGPUMgr* model_gpu_mgr{};
  const std::filesystem::path* resource_dir{};
  const EngineTime* time{};
  glm::uvec2 output_extent{};
  uint64_t frame_index{};
  uint32_t curr_frame_in_flight_idx{};
  bool imgui_ui_active{};
};

}  // namespace teng::engine
