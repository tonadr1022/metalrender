#pragma once

#include <vector>

#include "../../common/ScenePresets.hpp"
#include "../DemoSceneEcsBridge.hpp"
#include "../TestDebugScenes.hpp"
#include "FpsCameraController.hpp"
#include "gfx/renderer/MeshletRenderer.hpp"

namespace teng::gfx {

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

  void set_meshlet_renderer(MeshletRenderer* renderer) { meshlet_gpu_ = renderer; }

  void fill_render_tooling(MeshletSceneRenderTooling& out);

  void add_render_graph_passes() override {}

  [[nodiscard]] ViewData prepare_view_data();

 private:
  void load_scene_presets();
  void apply_preset(size_t idx);
  void author_current_demo_preset();

  void update_toward_light_effective(const TestSceneContext& ctx);

  MeshletRenderer* meshlet_gpu_{};

  glm::vec3 toward_light_manual_{0.35f, 1.f, 0.4f};
  glm::vec3 toward_light_effective_{0.35f, 1.f, 0.4f};
  bool day_night_cycle_{false};
  bool day_night_cycle_paused_{false};
  float day_cycle_time_sec_{0.f};
  float day_cycle_period_sec_{120.f};

  FpsCameraController fps_camera_;
  std::vector<teng::demo_scenes::DemoScenePresetData> scene_presets_;
  teng::gfx::demo_scene_compat::DemoSceneEntityGuids demo_entity_guids_{};
  size_t current_preset_index_{0};
  bool demo_preset_authoring_pending_{false};
  bool demo_preset_authored_{false};
};
}  // namespace teng::gfx
