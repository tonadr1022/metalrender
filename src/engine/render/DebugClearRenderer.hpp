#pragma once

#include <glm/ext/vector_float4.hpp>

#include "engine/render/IRenderer.hpp"

namespace teng::engine {

class DebugClearRenderer final : public IRenderer {
 public:
  explicit DebugClearRenderer(glm::vec4 clear_color = {0.03f, 0.04f, 0.055f, 1.f});

  void render(RenderFrameContext& frame, const RenderScene& scene) override;

  [[nodiscard]] const RenderScene* last_scene() const { return last_scene_; }

 private:
  glm::vec4 clear_color_;
  const RenderScene* last_scene_{};
};

}  // namespace teng::engine
