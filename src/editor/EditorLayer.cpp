#include "editor/EditorLayer.hpp"

#include <format>
#include <string>
#include <utility>

#include "engine/scene/Scene.hpp"
#include "imgui.h"

namespace teng::editor {

namespace {

[[nodiscard]] std::string scene_id_label(teng::engine::SceneId id) {
  return id.is_valid() ? std::to_string(id.value) : std::string{"none"};
}

[[nodiscard]] std::string entity_guid_label(
    const std::optional<teng::engine::EntityGuid>& selected) {
  if (!selected) {
    return "none";
  }
  return std::format("{:016x}", selected->value);
}

}  // namespace

EditorLayer::EditorLayer(engine::SceneId edit_scene_id,
                         std::optional<std::filesystem::path> scene_path)
    : edit_scene_id_(edit_scene_id), scene_path_(std::move(scene_path)) {}

void EditorLayer::on_attach(engine::EngineContext& ctx) {
  (void)session_.bind(ctx, edit_scene_id_, scene_path_);
}

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
      if (ImGui::MenuItem("Save", nullptr, false, session_.can_save())) {
        (void)session_.save();
      }
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
  if (session_.bound()) {
    const engine::Scene& edit_scene = session_.document_controller().document().scene();
    ImGui::Text("Edit scene: %s", edit_scene.name().c_str());
  } else {
    const engine::Scene* active_scene = ctx.scenes().active_scene();
    if (active_scene) {
      ImGui::Text("Active scene: %s", active_scene->name().c_str());
    } else {
      ImGui::TextUnformatted("No edit document");
    }
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
  ImGui::Text("Mode: %s", to_string(session_.mode()));
  if (active_scene) {
    ImGui::Text("Active scene id: %s", scene_id_label(active_scene->id()).c_str());
    ImGui::Text("Active scene name: %s", active_scene->name().c_str());
  } else {
    ImGui::TextUnformatted("Active scene id: none");
  }

  if (session_.bound()) {
    const EditorDocumentController& document = session_.document_controller();
    ImGui::Separator();
    ImGui::Text("Edit scene id: %s", scene_id_label(document.edit_scene_id()).c_str());
    ImGui::Text("Path: %s", document.path() ? document.path()->string().c_str() : "(unsaved)");
    ImGui::Text("Dirty: %s", document.dirty() ? "yes" : "no");
    ImGui::Text("Revision: %llu", static_cast<unsigned long long>(document.revision()));
    ImGui::Text("Saved revision: %llu", static_cast<unsigned long long>(document.saved_revision()));
  } else {
    ImGui::Separator();
    ImGui::TextUnformatted("Edit document: unbound");
  }
  ImGui::Text("Selected: %s", entity_guid_label(session_.selection().selected()).c_str());
  ImGui::TextWrapped("Status: %s", session_.last_status().c_str());
  ImGui::End();
}

}  // namespace teng::editor
