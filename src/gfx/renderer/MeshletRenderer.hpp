#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include "core/Math.hpp"
#include "engine/render/IRenderer.hpp"
#include "gfx/GPUFrameAllocator2.hpp"
#include "gfx/renderer/AlphaMaskType.hpp"
#include "gfx/renderer/MeshletCsmRenderer.hpp"
#include "gfx/renderer/MeshletResourceCompatibility.hpp"
#include "gfx/rhi/Config.hpp"
#include "hlsl/shared_cull_data.h"
#include "hlsl/shared_globals.h"

namespace teng::engine {
struct RenderScene;
}  // namespace teng::engine

namespace teng::gfx {

namespace rhi {
class Device;
}

struct MeshletImguiFrameSnapshot {
  rhi::Device* device{};
  uint32_t curr_frame_in_flight_idx{};
  uint32_t frames_in_flight{1};
};

class MeshletDrawPrep;
class MeshletDepthPyramid;

struct MeshletSceneRenderTooling {
  ViewData view{};
  glm::vec3 toward_light_effective{0.35f, 1.f, 0.4f};
};

class MeshletRenderer final : public engine::IRenderer {
 public:
  MeshletRenderer();
  ~MeshletRenderer() override;

  void set_model_path_resolver(MeshletResourceCompatibility::ResolveModelPathFn fn);
  void set_csm_scene_defaults(const MeshletCsmRenderer::SceneDefaults& defaults);

  void set_next_frame_tooling(const MeshletSceneRenderTooling& tooling) { next_tooling_ = tooling; }

  void render(engine::RenderFrameContext& frame, const engine::RenderScene& scene) override;
  void on_resize(engine::RenderFrameContext& frame) override;

  void shutdown_gpu();
  void imgui_gpu_panels();

 private:
  void lazy_init(const engine::RenderFrameContext& frame);
  void shutdown_subsystems();
  void make_depth_pyramid_tex(const engine::RenderFrameContext& frame);
  void bake_swapchain_clear(engine::RenderFrameContext& frame, std::string_view pass_name);

  [[nodiscard]] CullData prepare_cull_data_for_proj(const glm::mat4& proj, float z_near,
                                                    float z_far) const;
  [[nodiscard]] CullData prepare_cull_data(const ViewData& vd) const;
  [[nodiscard]] CullData prepare_cull_data_late(const ViewData& vd) const;

  MeshletResourceCompatibility resource_compat_;
  MeshletResourceCompatibility::ResolveModelPathFn resolve_model_path_;

  bool gpu_initialized_{false};
  std::optional<MeshletCsmRenderer::SceneDefaults> pending_csm_defaults_;
  std::unique_ptr<MeshletDrawPrep> draw_prep_;
  std::unique_ptr<MeshletDepthPyramid> depth_pyramid_;
  std::unique_ptr<MeshletCsmRenderer> csm_renderer_;
  std::optional<GPUFrameAllocator3> frame_uniform_gpu_allocator_;

  rhi::PipelineHandleHolder shade_pso_;
  std::array<rhi::PipelineHandleHolder, static_cast<size_t>(AlphaMaskType::Count)>
      meshlet_pso_early_{};
  std::array<rhi::PipelineHandleHolder, static_cast<size_t>(AlphaMaskType::Count)>
      meshlet_pso_late_{};
  rhi::BufferHandleHolder meshlet_vis_buf_;
  std::array<rhi::BufferHandleHolder, k_max_frames_in_flight> task_cmd_group_count_readback_{};
  std::array<rhi::BufferHandleHolder, k_max_frames_in_flight> visible_object_count_readback_{};
  rhi::BufferHandleHolder meshlet_stats_buf_readback_[k_max_frames_in_flight]{};

  static constexpr size_t k_meshlet_draw_stats_bytes = sizeof(uint32_t) * 4;

  bool reverse_z_{true};
  bool gpu_object_frustum_cull_{true};
  bool gpu_object_occlusion_cull_{true};
  uint32_t frame_num_{0};
  MeshletSceneRenderTooling next_tooling_{};

  std::optional<MeshletImguiFrameSnapshot> last_imgui_frame_;

  rhi::Device* gpu_device_{};
};

}  // namespace teng::gfx
