#pragma once

#include "engine/Engine.hpp"

namespace teng::editor {

class EditorLayer final : public engine::Layer {
 public:
  void on_imgui(engine::EngineContext& ctx) override;

 private:
  void draw_dockspace();
  void draw_viewport(engine::EngineContext& ctx);
  void draw_hierarchy(engine::EngineContext& ctx);
  void draw_inspector();
  void draw_stats(engine::EngineContext& ctx);
};

}  // namespace teng::editor
