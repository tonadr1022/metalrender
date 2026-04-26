#include "MeshletRendererTestScene.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "Window.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneIds.hpp"
#include "engine/scene/SceneManager.hpp"
#include "gfx/rhi/Swapchain.hpp"
#include "imgui.h"

namespace teng::gfx {

using namespace teng::demo_scenes;

MeshletRendererScene::MeshletRendererScene(const TestSceneContext& ctx) : ITestScene(ctx) {
  load_scene_presets();
}

void MeshletRendererScene::load_scene_presets() {
  scene_presets_.clear();
  append_default_scene_preset_data(scene_presets_, ctx_.resource_dir);
}

void MeshletRendererScene::author_current_demo_preset() {
  if (!ctx_.scene_manager) {
    return;
  }
  if (scene_presets_.empty() || current_preset_index_ >= scene_presets_.size()) {
    return;
  }
  demo_entity_guids_ = demo_scene_compat::apply_demo_preset_to_scene(
      *ctx_.scene_manager, scene_presets_[current_preset_index_], ctx_.resource_dir);
  demo_preset_authoring_pending_ = false;
  demo_preset_authored_ = true;
  if (auto* scene = ctx_.scene_manager->active_scene()) {
    sync_compatibility_ecs_scene(*scene);
  }
}

void MeshletRendererScene::apply_preset(size_t idx) {
  if (scene_presets_.empty() || idx >= scene_presets_.size()) {
    return;
  }
  auto& preset = scene_presets_[idx];

  if (meshlet_gpu_) {
    if (preset.csm_defaults.has_value()) {
      const auto& d = *preset.csm_defaults;
      meshlet_gpu_->set_csm_scene_defaults(MeshletCsmRenderer::SceneDefaults{
          .z_near = d.z_near,
          .z_far = d.z_far,
          .cascade_count = d.cascade_count,
          .split_lambda = d.split_lambda,
      });
    } else {
      meshlet_gpu_->set_csm_scene_defaults(MeshletCsmRenderer::SceneDefaults{});
    }
  }

  current_preset_index_ = idx;
  demo_preset_authoring_pending_ = true;
  demo_preset_authored_ = false;
  author_current_demo_preset();
}

void MeshletRendererScene::apply_demo_scene_preset(size_t index) {
  if (scene_presets_.empty()) {
    return;
  }
  const size_t idx = std::min(index, scene_presets_.size() - 1);
  apply_preset(idx);
}

void MeshletRendererScene::shutdown() {
  if (ctx_.window) {
    glfwSetInputMode(ctx_.window->get_handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  }
}

void MeshletRendererScene::on_frame(const TestSceneContext& ctx) {
  (void)ctx;
  sync_mouse_capture_from_ecs();
}

void MeshletRendererScene::sync_compatibility_ecs_scene(engine::Scene& scene) {
  if (demo_preset_authoring_pending_) {
    author_current_demo_preset();
  }
  if (!demo_preset_authored_) {
    return;
  }

  demo_scene_compat::sync_demo_light_tooling(scene, demo_entity_guids_.light,
                                             {
                                                 .direction = glm::vec3{0.35f, 1.f, 0.4f},
                                                 .color = glm::vec3{1.f},
                                                 .intensity = 1.f,
                                             });
}

void MeshletRendererScene::sync_mouse_capture_from_ecs() {
  if (!ctx_.window || !ctx_.scene_manager || !demo_entity_guids_.camera.is_valid()) {
    return;
  }
  const engine::Scene* scene = ctx_.scene_manager->active_scene();
  if (!scene) {
    return;
  }
  const auto* controller = scene->get_fps_camera_controller(demo_entity_guids_.camera);
  if (!controller || controller->mouse_captured == applied_mouse_captured_) {
    return;
  }
  applied_mouse_captured_ = controller->mouse_captured;
  glfwSetInputMode(ctx_.window->get_handle(), GLFW_CURSOR,
                   applied_mouse_captured_ ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void MeshletRendererScene::on_cursor_pos(double x, double y) {
  (void)x;
  (void)y;
}

void MeshletRendererScene::on_key_event(int key, int action, int mods) {
  (void)key;
  (void)action;
  (void)mods;
}

void MeshletRendererScene::on_imgui() {
  ImGui::Begin("Meshlet renderer");
  if (!scene_presets_.empty()) {
    ImGui::SeparatorText("Scene presets");
    static size_t scene_preset_selection = 0;
    ASSERT(!scene_presets_.empty());
    const float list_h = ImGui::GetTextLineHeightWithSpacing() * 7.0f;
    if (ImGui::BeginListBox("##scene_presets", ImVec2(-FLT_MIN, list_h))) {
      for (size_t i = 0; i < scene_presets_.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const bool selected = (scene_preset_selection == i);
        if (ImGui::Selectable(scene_presets_[i].name.c_str(), selected,
                              ImGuiSelectableFlags_AllowDoubleClick)) {
          scene_preset_selection = i;
          if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            apply_preset(i);
          }
        }
        ImGui::PopID();
      }
      ImGui::EndListBox();
    }
    if (ImGui::Button("Load preset", ImVec2(-FLT_MIN, 0))) {
      apply_preset(scene_preset_selection);
    }
    ImGui::Separator();
  }
  if (meshlet_gpu_) {
    meshlet_gpu_->imgui_gpu_panels();
  }
  ImGui::End();
}

}  // namespace teng::gfx
