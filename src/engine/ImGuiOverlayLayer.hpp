#pragma once

#include "engine/Engine.hpp"

namespace teng::engine {

class ImGuiOverlayLayer final : public Layer {
 public:
  void on_attach(EngineContext& ctx) override;
  void on_detach(EngineContext& ctx) override;
  void on_update(EngineContext& ctx, const EngineTime& time) override;
  void on_imgui(EngineContext& ctx) override;
  void on_render(EngineContext& ctx) override;
  void on_end_frame(EngineContext& ctx) override;
  void on_key_event(EngineContext& ctx, int key, int action, int mods) override;

 private:
  void init_imgui(EngineContext& ctx);
  void shutdown_imgui();

  bool imgui_initialized_{};
  bool frame_started_{};
};

}  // namespace teng::engine
