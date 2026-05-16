#pragma once

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_uint2.hpp>

#include "engine/render/RenderScene.hpp"

namespace teng::engine {
struct EngineInputSnapshot;
}

namespace teng::editor {

class EditorViewportCamera {
 public:
  void update(const teng::engine::EngineInputSnapshot& input, float delta_seconds,
              bool viewport_accepts_input);

  void look_at(glm::vec3 position, glm::vec3 target);

  [[nodiscard]] teng::engine::SceneRenderView make_render_view(glm::uvec2 output_extent) const;

  [[nodiscard]] const glm::vec3& position() const { return position_; }
  [[nodiscard]] float yaw_degrees() const { return yaw_degrees_; }
  [[nodiscard]] float pitch_degrees() const { return pitch_degrees_; }

 private:
  [[nodiscard]] glm::vec3 front() const;

  glm::vec3 position_{0.f, 1.5f, 5.f};
  float yaw_degrees_{-90.f};
  float pitch_degrees_{0.f};
  float move_speed_{6.f};
  float mouse_sensitivity_{0.12f};
  float fov_y_radians_{1.0471975512f};
  float near_plane_{0.1f};
  float far_plane_{10'000.f};
};

}  // namespace teng::editor
