#pragma once

#include <vector>

#include "../../common/ScenePresets.hpp"
#include "../DemoSceneEcsBridge.hpp"
#include "../TestDebugScenes.hpp"
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

  void on_swapchain_resize() override {}

  void apply_demo_scene_preset(size_t index) override;
  void sync_compatibility_ecs_scene(teng::engine::Scene& scene) override;

  void set_meshlet_renderer(MeshletRenderer* renderer) { meshlet_gpu_ = renderer; }

  void add_render_graph_passes() override {}

 private:
  void load_scene_presets();
  void apply_preset(size_t idx);
  void author_current_demo_preset();
  void sync_mouse_capture_from_ecs();

  MeshletRenderer* meshlet_gpu_{};
  std::vector<teng::demo_scenes::DemoScenePresetData> scene_presets_;
  teng::gfx::demo_scene_compat::DemoSceneEntityGuids demo_entity_guids_{};
  size_t current_preset_index_{0};
  bool demo_preset_authoring_pending_{false};
  bool demo_preset_authored_{false};
  bool applied_mouse_captured_{false};
};
}  // namespace teng::gfx
