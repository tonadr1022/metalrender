#include "engine/render/RenderScene.hpp"

#include <cmath>
#include <glm/glm.hpp>
#include <glm/trigonometric.hpp>

namespace teng::engine {

namespace {

const RenderCamera* pick_runtime_camera(const RenderScene& scene) {
  for (const RenderCamera& camera : scene.cameras) {
    if (camera.primary) {
      return &camera;
    }
  }
  return scene.cameras.empty() ? nullptr : &scene.cameras.front();
}

glm::mat4 infinite_perspective_projection(float fov_y, float aspect, float z_near) {
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

SceneRenderView make_runtime_scene_render_view(const RenderScene& scene, glm::uvec2 fallback_extent) {
  const RenderCamera* camera = pick_runtime_camera(scene);
  if (!camera) {
    return {};
  }

  const glm::uvec2 extent = scene.frame.output_extent.x > 0 && scene.frame.output_extent.y > 0
                                ? scene.frame.output_extent
                                : fallback_extent;
  if (extent.x == 0 || extent.y == 0) {
    return {};
  }

  const float aspect = static_cast<float>(extent.x) / static_cast<float>(extent.y);
  const float fov_y = camera->fov_y > 1e-6f ? camera->fov_y : glm::radians(60.f);
  const float near_plane = camera->z_near > 0.f ? camera->z_near : 0.1f;
  const float far_plane = camera->z_far > near_plane ? camera->z_far : 10'000.f;

  return SceneRenderView{
      .view = glm::inverse(camera->local_to_world),
      .projection = infinite_perspective_projection(fov_y, aspect, near_plane),
      .position = glm::vec3{camera->local_to_world * glm::vec4{0.f, 0.f, 0.f, 1.f}},
      .near_plane = near_plane,
      .far_plane = far_plane,
      .kind = RenderViewKind::Runtime,
      .valid = true,
  };
}

}  // namespace teng::engine
