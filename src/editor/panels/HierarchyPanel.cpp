#include "editor/panels/HierarchyPanel.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <string>

#include "editor/EditorSession.hpp"
#include "engine/scene/Scene.hpp"
#include "imgui.h"

namespace teng::editor {

namespace {

[[nodiscard]] std::string entity_imgui_id(teng::engine::EntityGuid guid) {
  return std::format("{}##{:016x}", guid.value, guid.value);
}

[[nodiscard]] std::string entity_label(flecs::entity entity, teng::engine::EntityGuid guid) {
  const auto* name = entity.try_get<teng::engine::Name>();
  if (name && !name->value.empty()) {
    return name->value;
  }
  return std::format("Entity {:016x}", guid.value);
}

}  // namespace

void HierarchyPanel::draw(EditorSession& session) {
  ImGui::Begin("Hierarchy");

  if (!session.bound()) {
    invalidate_cache();
    ImGui::TextUnformatted("No edit document");
    ImGui::End();
    return;
  }

  const EditorDocumentController& document = session.document_controller();
  const engine::Scene& scene = document.document().scene();
  if (!cache_valid_ || cached_scene_id_ != document.edit_scene_id() ||
      cached_revision_ != document.revision()) {
    rebuild_rows(session);
  }

  ImGui::Text("Edit scene: %s", scene.name().c_str());

  ImGui::BeginDisabled(!session.can_create_entity());
  if (ImGui::Button("Create", ImVec2{80.f, 0.f})) {
    (void)session.create_entity();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  const bool can_delete = session.can_delete_selected_entity();
  ImGui::BeginDisabled(!can_delete);
  if (ImGui::Button("Delete", ImVec2{80.f, 0.f})) {
    (void)session.delete_selected_entity();
  }
  ImGui::EndDisabled();

  ImGui::Separator();

  const std::optional<engine::EntityGuid>& selected = session.selection().selected();
  for (const Row& entity : rows_) {
    const bool is_selected = selected && *selected == entity.guid;
    const std::string id = entity_imgui_id(entity.guid);
    ImGui::PushID(id.c_str());
    if (ImGui::Selectable(entity.label.c_str(), is_selected)) {
      session.selection().select(entity.guid);
    }
    ImGui::PopID();
  }

  ImGui::End();
}

void HierarchyPanel::rebuild_rows(EditorSession& session) {
  const EditorDocumentController& document = session.document_controller();
  const engine::Scene& scene = document.document().scene();

  rows_.clear();
  scene.world().each([this](flecs::entity entity, const engine::EntityGuidComponent& guid_component) {
    rows_.push_back(Row{
        .guid = guid_component.guid,
        .label = entity_label(entity, guid_component.guid),
    });
  });

  std::ranges::sort(rows_, [](const Row& lhs, const Row& rhs) {
    if (lhs.label == rhs.label) {
      return lhs.guid.value < rhs.guid.value;
    }
    return lhs.label < rhs.label;
  });

  cached_scene_id_ = document.edit_scene_id();
  cached_revision_ = document.revision();
  cache_valid_ = true;
}

void HierarchyPanel::invalidate_cache() {
  rows_.clear();
  cached_scene_id_ = {};
  cached_revision_ = 0;
  cache_valid_ = false;
}

}  // namespace teng::editor
