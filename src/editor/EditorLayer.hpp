#pragma once

#include <filesystem>
#include <optional>

#include "editor/EditorSession.hpp"
#include "editor/EditorViewportCamera.hpp"
#include "editor/EditorViewportTarget.hpp"
#include "editor/panels/HierarchyPanel.hpp"
#include "engine/Engine.hpp"

namespace teng::editor {

class EditorLayer final : public engine::Layer {
 public:
  EditorLayer(engine::SceneId edit_scene_id, std::optional<std::filesystem::path> scene_path);

  void on_attach(engine::EngineContext& ctx) override;
  void on_imgui(engine::EngineContext& ctx) override;
  void on_render_scene(engine::EngineContext& ctx) override;

 private:
  void draw_dockspace();
  void draw_viewport(engine::EngineContext& ctx);
  void draw_inspector();
  void draw_stats(engine::EngineContext& ctx);

  engine::SceneId edit_scene_id_;
  std::optional<std::filesystem::path> scene_path_;
  EditorSession session_;
  HierarchyPanel hierarchy_panel_;
  EditorViewportCamera viewport_camera_;
  EditorViewportTarget viewport_target_;
  glm::uvec2 viewport_pixel_size_{};
  bool viewport_accepts_input_{};
  bool dock_layout_initialized_{};
};

}  // namespace teng::editor
