#pragma once

#include <cstdint>
#include <filesystem>

#include <glm/ext/vector_uint2.hpp>

namespace teng::gfx {
class RenderGraph;
class ShaderManager;

namespace rhi {
class Device;
class Swapchain;
}  // namespace rhi
}  // namespace teng::gfx

namespace teng::engine {

struct EngineTime;

struct RenderFrameContext {
  gfx::rhi::Device* device{};
  gfx::rhi::Swapchain* swapchain{};
  gfx::RenderGraph* render_graph{};
  gfx::ShaderManager* shader_mgr{};
  const std::filesystem::path* resource_dir{};
  const EngineTime* time{};
  glm::uvec2 output_extent{};
  std::uint64_t frame_index{};
  std::uint32_t curr_frame_in_flight_idx{};
  bool imgui_ui_active{};
};

}  // namespace teng::engine
