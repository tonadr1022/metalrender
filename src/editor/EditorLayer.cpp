#include "editor/EditorLayer.hpp"

#include "engine/scene/Scene.hpp"
#include "imgui.h"

namespace teng::editor {

void EditorLayer::on_imgui(engine::EngineContext& ctx) {
  if (!ctx.imgui_enabled()) {
    return;
  }

  draw_dockspace();
  draw_viewport(ctx);
  draw_hierarchy(ctx);
  draw_inspector();
  draw_stats(ctx);
}

void EditorLayer::draw_dockspace() {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::SetNextWindowViewport(viewport->ID);

  const ImGuiWindowFlags window_flags =
      ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
  ImGui::Begin("Teng Editor Dockspace", nullptr, window_flags);
  ImGui::PopStyleVar(3);

  if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      ImGui::MenuItem("Save", nullptr, false, false);
      ImGui::MenuItem("Reload", nullptr, false, false);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Play")) {
      ImGui::MenuItem("Play", nullptr, false, false);
      ImGui::MenuItem("Stop", nullptr, false, false);
      ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
  }

  const ImGuiID dockspace_id = ImGui::GetID("TengEditorDockspace");
  ImGui::DockSpace(dockspace_id, ImVec2{0.0f, 0.0f}, ImGuiDockNodeFlags_PassthruCentralNode);
  ImGui::End();
}

void EditorLayer::draw_viewport(engine::EngineContext&) {
  ImGui::Begin("Viewport");
  ImGui::TextUnformatted("Editor viewport placeholder");
  ImGui::End();
}

void EditorLayer::draw_hierarchy(engine::EngineContext& ctx) {
  ImGui::Begin("Hierarchy");
  const engine::Scene* active_scene = ctx.scenes().active_scene();
  if (active_scene) {
    ImGui::Text("Active scene: %s", active_scene->name().c_str());
  } else {
    ImGui::TextUnformatted("No active scene");
  }
  ImGui::End();
}

void EditorLayer::draw_inspector() {
  ImGui::Begin("Inspector");
  ImGui::TextUnformatted("No selection");
  ImGui::End();
}

void EditorLayer::draw_stats(engine::EngineContext& ctx) {
  ImGui::Begin("Stats");
  const engine::Scene* active_scene = ctx.scenes().active_scene();
  ImGui::Text("Frame: %llu", static_cast<unsigned long long>(ctx.time().frame_index));
  if (active_scene) {
    ImGui::Text("Scene id: %llu", static_cast<unsigned long long>(active_scene->id().value));
    ImGui::Text("Scene name: %s", active_scene->name().c_str());
  } else {
    ImGui::TextUnformatted("Scene id: none");
  }
  ImGui::End();
}

}  // namespace teng::editor
