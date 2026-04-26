#pragma once

#include <array>
#include <vector>

#include "../../common/ScenePresets.hpp"
#include "../DemoSceneEcsBridge.hpp"
#include "../TestDebugScenes.hpp"
#include "FpsCameraController.hpp"
#include "MeshletCsmRenderer.hpp"
#include "MeshletDepthPyramid.hpp"
#include "MeshletDrawPrep.hpp"
#include "gfx/RendererTypes.hpp"
#include "gfx/renderer/AlphaMaskType.hpp"
#include "gfx/rhi/Texture.hpp"
#include "hlsl/shared_cull_data.h"
#include "hlsl/shared_globals.h"

namespace teng::gfx {

class RenderGraph;
class ModelGPUMgr;

class MeshletRendererScene final : public ITestScene {
 public:
  explicit MeshletRendererScene(const TestSceneContext& ctx);

  void shutdown() override;

  void on_frame(const TestSceneContext& ctx) override;

  void on_cursor_pos(double x, double y) override;

  void on_key_event(int key, int action, int mods) override;

  void on_imgui() override;

  void on_swapchain_resize() override;

  void apply_demo_scene_preset(size_t index) override;
  void sync_compatibility_ecs_scene(teng::engine::Scene& scene) override;

  [[nodiscard]] ViewData prepare_view_data();
  [[nodiscard]] CullData prepare_cull_data(const ViewData& vd) const;
  [[nodiscard]] CullData prepare_cull_data_late(const ViewData& vd) const;
  [[nodiscard]] CullData prepare_cull_data_for_proj(const glm::mat4& proj, float z_near,
                                                    float z_far) const;

  void render();
  void add_render_graph_passes() override;

 private:
  void load_scene_presets();
  void apply_preset(size_t idx);
  void author_current_demo_preset();

  void make_depth_pyramid_tex();

  void update_toward_light_effective(const TestSceneContext& ctx);

  glm::vec3 toward_light_manual_{0.35f, 1.f, 0.4f};
  glm::vec3 toward_light_effective_{0.35f, 1.f, 0.4f};
  bool day_night_cycle_{false};
  bool day_night_cycle_paused_{false};
  float day_cycle_time_sec_{0.f};
  float day_cycle_period_sec_{120.f};

  bool reverse_z_{true};
  MeshletDrawPrep draw_prep_;
  MeshletDepthPyramid depth_pyramid_;
  MeshletCsmRenderer csm_renderer_;
  rhi::PipelineHandleHolder shade_pso_;
  std::array<rhi::PipelineHandleHolder, static_cast<size_t>(AlphaMaskType::Count)>
      meshlet_pso_early_;
  std::array<rhi::PipelineHandleHolder, static_cast<size_t>(AlphaMaskType::Count)>
      meshlet_pso_late_;
  rhi::BufferHandleHolder meshlet_vis_buf_;
  FpsCameraController fps_camera_;
  std::vector<teng::demo_scenes::DemoScenePresetData> scene_presets_;
  teng::gfx::demo_scene_compat::DemoSceneEntityGuids demo_entity_guids_{};
  size_t current_preset_index_{0};
  bool demo_preset_authoring_pending_{false};
  bool demo_preset_authored_{false};
  bool gpu_object_frustum_cull_{true};
  bool gpu_object_occlusion_cull_{true};
  std::array<rhi::BufferHandleHolder, k_max_frames_in_flight> task_cmd_group_count_readback_;
  std::array<rhi::BufferHandleHolder, k_max_frames_in_flight> visible_object_count_readback_;
  GPUFrameAllocator3 frame_uniform_gpu_allocator_;
  uint32_t frame_num_{0};

  static constexpr size_t k_meshlet_draw_stats_bytes = sizeof(uint32_t) * 4;
  rhi::BufferHandleHolder meshlet_stats_buf_readback_[k_max_frames_in_flight];
};
}  // namespace teng::gfx
