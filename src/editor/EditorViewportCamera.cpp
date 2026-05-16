#include "editor/EditorViewportCamera.hpp"

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include "engine/Input.hpp"

namespace teng::editor {

namespace {

[[nodiscard]] bool any_key_down(const teng::engine::EngineInputSnapshot& input, int a, int b) {
  return input.key_down(a) || input.key_down(b);
}

[[nodiscard]] glm::mat4 infinite_perspective_projection(float fov_y, float aspect, float z_near) {
  const float f = 1.f / std::tan(fov_y / 2.f);
  // clang-format off
  return {
      f / aspect, 0.f, 0.f, 0.f,
      0.f, f, 0.f, 0.f,
      0.f, 0.f, 0.f, -1.f,
      0.f, 0.f, z_near, 0.f};
  // clang-format on
}

}  // namespace

void EditorViewportCamera::update(const teng::engine::EngineInputSnapshot& input,
                                  float delta_seconds, bool viewport_accepts_input) {
  if (!viewport_accepts_input) {
    return;
  }

  yaw_degrees_ += input.cursor_delta.x * mouse_sensitivity_;
  pitch_degrees_ += input.cursor_delta.y * mouse_sensitivity_;
  pitch_degrees_ = std::clamp(pitch_degrees_, -89.f, 89.f);

  glm::vec3 movement{};
  const glm::vec3 forward = front();
  const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3{0.f, 1.f, 0.f}));

  if (any_key_down(input, teng::engine::KeyCode::W, teng::engine::KeyCode::I)) {
    movement += forward;
  }
  if (any_key_down(input, teng::engine::KeyCode::S, teng::engine::KeyCode::K)) {
    movement -= forward;
  }
  if (any_key_down(input, teng::engine::KeyCode::A, teng::engine::KeyCode::J)) {
    movement -= right;
  }
  if (any_key_down(input, teng::engine::KeyCode::D, teng::engine::KeyCode::L)) {
    movement += right;
  }
  if (any_key_down(input, teng::engine::KeyCode::Y, teng::engine::KeyCode::R)) {
    movement += glm::vec3{0.f, 1.f, 0.f};
  }
  if (any_key_down(input, teng::engine::KeyCode::H, teng::engine::KeyCode::F)) {
    movement -= glm::vec3{0.f, 1.f, 0.f};
  }

  if (glm::dot(movement, movement) > 0.0001f) {
    position_ += glm::normalize(movement) * move_speed_ * delta_seconds;
  }
}

void EditorViewportCamera::look_at(const glm::vec3 position, const glm::vec3 target) {
  position_ = position;
  const glm::vec3 forward = glm::normalize(target - position);
  pitch_degrees_ = glm::degrees(std::asin(std::clamp(forward.y, -1.f, 1.f)));
  yaw_degrees_ = glm::degrees(std::atan2(forward.z, forward.x));
}

teng::engine::SceneRenderView EditorViewportCamera::make_render_view(glm::uvec2 output_extent) const {
  if (output_extent.x == 0 || output_extent.y == 0) {
    return {};
  }

  const float aspect = static_cast<float>(output_extent.x) / static_cast<float>(output_extent.y);
  return teng::engine::SceneRenderView{
      .view = glm::lookAt(position_, position_ + front(), glm::vec3{0.f, 1.f, 0.f}),
      .projection = infinite_perspective_projection(fov_y_radians_, aspect, near_plane_),
      .position = position_,
      .near_plane = near_plane_,
      .far_plane = far_plane_,
      .kind = teng::engine::RenderViewKind::Editor,
      .valid = true,
  };
}

glm::vec3 EditorViewportCamera::front() const {
  glm::vec3 direction;
  direction.x = std::cos(glm::radians(yaw_degrees_)) * std::cos(glm::radians(pitch_degrees_));
  direction.y = std::sin(glm::radians(pitch_degrees_));
  direction.z = std::sin(glm::radians(yaw_degrees_)) * std::cos(glm::radians(pitch_degrees_));
  return glm::normalize(direction);
}

}  // namespace teng::editor
