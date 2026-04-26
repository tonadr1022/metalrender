#include "MeshletRendererTestScene.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

#include "Window.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneIds.hpp"
#include "engine/scene/SceneManager.hpp"
#include "gfx/rhi/Swapchain.hpp"
#include "imgui.h"

namespace teng::gfx {

using namespace teng::demo_scenes;

namespace {

glm::vec3 safe_normalize_toward_light(const glm::vec3& v) {
  const float s2 = glm::dot(v, v);
  if (s2 < 1e-12f) {
    return glm::normalize(glm::vec3(0.35f, 1.f, 0.4f));
  }
  return v * (1.f / std::sqrt(s2));
}

}  // namespace

void MeshletRendererScene::update_toward_light_effective(const TestSceneContext& ctx) {
  if (day_night_cycle_ && !day_night_cycle_paused_) {
    const float period = std::max(1e-4f, day_cycle_period_sec_);
    day_cycle_time_sec_ = std::fmodf(day_cycle_time_sec_ + ctx.delta_time_sec, period);
    if (day_cycle_time_sec_ < 0.f) {
      day_cycle_time_sec_ += period;
    }
  }

  if (day_night_cycle_) {
    const float period = std::max(1e-4f, day_cycle_period_sec_);
    const float phase_wrapped = std::fmodf(day_cycle_time_sec_, period);
    const float t = 2.0f * std::numbers::pi_v<float> * (phase_wrapped / period) -
                    0.5f * std::numbers::pi_v<float>;
    const glm::vec3 raw(0.35f * std::cos(t), std::sin(t), 0.4f * std::cos(t * 0.5f));
    toward_light_effective_ = safe_normalize_toward_light(raw);
  } else {
    toward_light_effective_ = safe_normalize_toward_light(toward_light_manual_);
  }
}

MeshletRendererScene::MeshletRendererScene(const TestSceneContext& ctx) : ITestScene(ctx) {
  load_scene_presets();
  toward_light_effective_ = safe_normalize_toward_light(toward_light_manual_);
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
  fps_camera_.camera() = preset.cam;
  fps_camera_.camera().calc_vectors();

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

void MeshletRendererScene::on_swapchain_resize() {}

void MeshletRendererScene::shutdown() {
  if (ctx_.window) {
    fps_camera_.set_mouse_captured(ctx_.window->get_handle(), false);
  }
}

void MeshletRendererScene::on_frame(const TestSceneContext& ctx) {
  update_toward_light_effective(ctx);
  const bool imgui_blocks =
      ctx.imgui_ui_active && (ImGui::GetIO().WantTextInput || ImGui::GetIO().WantCaptureKeyboard);
  if (ctx.window) {
    fps_camera_.update(ctx.window->get_handle(), ctx.delta_time_sec, imgui_blocks);
  }
}

void MeshletRendererScene::sync_compatibility_ecs_scene(engine::Scene& scene) {
  if (demo_preset_authoring_pending_) {
    author_current_demo_preset();
  }
  if (!demo_preset_authored_) {
    return;
  }

  Camera& app_camera = fps_camera_.camera();
  app_camera.calc_vectors();
  demo_scene_compat::sync_demo_camera_tooling(scene, demo_entity_guids_.camera, app_camera);
  demo_scene_compat::sync_demo_light_tooling(scene, demo_entity_guids_.light,
                                             {
                                                 .direction = toward_light_effective_,
                                                 .color = glm::vec3{1.f},
                                                 .intensity = 1.f,
                                             });
}

void MeshletRendererScene::on_cursor_pos(double x, double y) { fps_camera_.on_cursor_pos(x, y); }

void MeshletRendererScene::on_key_event(int key, int action, int mods) {
  (void)mods;
  if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE && ctx_.window) {
    fps_camera_.toggle_mouse_capture(ctx_.window->get_handle());
  }
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
  ImGui::SeparatorText("Lighting");
  {
    const bool was_day_night = day_night_cycle_;
    ImGui::BeginDisabled(day_night_cycle_);
    ImGui::DragFloat3("Toward light (world)", &toward_light_manual_.x, 0.01f);
    ImGui::EndDisabled();
    ImGui::Checkbox("Day / night cycle", &day_night_cycle_);
    if (was_day_night && !day_night_cycle_) {
      toward_light_manual_ = toward_light_effective_;
    }
    ImGui::BeginDisabled(!day_night_cycle_);
    ImGui::Checkbox("Pause cycle", &day_night_cycle_paused_);
    ImGui::SliderFloat("Day length (s)", &day_cycle_period_sec_, 10.0f, 600.0f);
    ImGui::EndDisabled();
    ImGui::Text("Effective (normalized): %.3f, %.3f, %.3f", toward_light_effective_.x,
                toward_light_effective_.y, toward_light_effective_.z);
  }
  if (meshlet_gpu_) {
    meshlet_gpu_->imgui_gpu_panels();
  }
  ImGui::End();
}

}  // namespace teng::gfx
