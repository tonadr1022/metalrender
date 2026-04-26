#pragma once

#include <cstdint>
#include <filesystem>
#include <glm/ext/vector_uint2.hpp>

#include "gfx/RenderGraph.hpp"

namespace teng::gfx {
class ModelGPUMgr;
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
class SceneManager;

struct RenderFrameContext {
  gfx::rhi::Device* device{};
  gfx::rhi::Swapchain* swapchain{};
  Window* window{};
  gfx::RenderGraph* render_graph{};
  gfx::ShaderManager* shader_mgr{};
  gfx::BufferCopyMgr* buffer_copy{};
  gfx::GPUFrameAllocator3* frame_staging{};
  gfx::ModelGPUMgr* model_gpu_mgr{};
  SceneManager* scenes{};
  const std::filesystem::path* resource_dir{};
  const EngineTime* time{};
  glm::uvec2 output_extent{};
  gfx::RGResourceId curr_swapchain_rg_id{};
  uint64_t frame_index{};
  uint32_t curr_frame_in_flight_idx{};
  bool imgui_ui_active{};
};

}  // namespace teng::engine
