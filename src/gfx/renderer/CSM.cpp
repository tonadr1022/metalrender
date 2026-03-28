#include "CSM.hpp"

#include <span>

#include "core/EAssert.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"

namespace TENG_NAMESPACE {

namespace {

void calc_frustum_corners_world_space(std::span<glm::vec4> corners, const glm::mat4& vp_matrix) {
  const auto inv_vp = glm::inverse(vp_matrix);
  for (uint32_t z = 0, i = 0; z < 2; z++) {
    for (uint32_t y = 0; y < 2; y++) {
      for (uint32_t x = 0; x < 2; x++, i++) {
        glm::vec4 pt = inv_vp * glm::vec4((2.f * x) - 1.f, (2.f * y) - 1.f, (float)z, 1.f);
        corners[i] = pt / pt.w;
      }
    }
  }
}

glm::mat4 calc_light_space_vp(const glm::mat4& cam_view, const glm::mat4& cam_proj,
                              glm::vec3 light_dir, float, glm::mat4& light_proj) {
  glm::vec3 center;
  std::array<glm::vec4, 8> corners;
  calc_frustum_corners_world_space(corners, cam_proj * cam_view);
  for (auto v : corners) {
    center += glm::vec3(v);
  }
  center /= 8;

  glm::mat4 light_view = glm::lookAt(center + light_dir, center, glm::vec3(0, 1, 0));
  glm::vec3 min{std::numeric_limits<float>::max()};
  glm::vec3 max{std::numeric_limits<float>::lowest()};
  for (auto corner : corners) {
    glm::vec3 c = light_view * corner;
    min.x = glm::min(min.x, c.x);
    max.x = glm::max(max.x, c.x);
    min.y = glm::min(min.y, c.y);
    max.y = glm::max(max.y, c.y);
    min.z = glm::min(min.z, c.z);
    max.z = glm::max(max.z, c.z);
  }

  float z_pad = 1.5;
  float z_padding = (max.z - min.z) * z_pad;
  min.z -= z_padding;
  max.z += z_padding;
  // if (min.z < 0) {
  //   min.z *= z_mult;
  // } else {
  //   min.z /= z_mult;
  // }
  // if (max.z < 0) {
  //   max.z /= z_mult;
  // } else {
  //   max.z *= z_mult;
  // }
  light_proj = glm::orthoRH_ZO(min.x, max.x, max.y, min.y, min.z, max.z);
  return light_proj * light_view;
}

void calc_csm_light_space_vp_matrices(std::span<glm::mat4> matrices,
                                      std::span<glm::mat4> proj_matrices, std::span<float> levels,
                                      const glm::mat4& cam_view, glm::vec3 light_dir, float fov_deg,
                                      float aspect, float cam_near, float cam_far,
                                      uint32_t shadow_map_res) {
  ASSERT((matrices.size() && levels.size() && matrices.size() - 1 == levels.size()));
  auto get_proj = [&](float near, float far) {
    auto mat = glm::perspective(glm::radians(fov_deg), aspect, near, far);
    mat[1][1] *= -1;
    return mat;
  };

  glm::vec3 dir = -glm::normalize(light_dir);
  matrices[0] = calc_light_space_vp(cam_view, get_proj(cam_near, levels[0]), dir, shadow_map_res,
                                    proj_matrices[0]);
  for (uint32_t i = 1; i < matrices.size() - 1; i++) {
    matrices[i] = calc_light_space_vp(cam_view, get_proj(levels[i - 1], levels[i]), dir,
                                      shadow_map_res, proj_matrices[i]);
  }
  matrices[matrices.size() - 1] =
      calc_light_space_vp(cam_view, get_proj(levels[levels.size() - 1], cam_far), dir,
                          shadow_map_res, proj_matrices[matrices.size() - 1]);
}

void calc_csm_data() {
  constexpr uint32_t k_max_cascade_levels = 4;
  uint32_t cascade_count_ = k_max_cascade_levels - 1;
  std::array<float, k_max_cascade_levels - 1> levels;
  float shadow_z_near_ = 0.1f;
  float shadow_z_far_ = 100.0f;
  float cascade_linear_factor_ = 0.95f;
  float fov_deg = 90.0f;
  float aspect_ratio = 1.0f;
  glm::vec3 light_dir = glm::vec3(0.0f, 1.0f, 0.0f);
  auto cam_view = glm::mat4{1.0f};
  for (uint32_t i = 0; i < cascade_count_ - 1; i++) {
    float p = (i + 1) / static_cast<float>(cascade_count_);
    float log_split = shadow_z_near_ * std::pow(shadow_z_far_ / shadow_z_near_, p);
    float linear_split = shadow_z_near_ + ((shadow_z_far_ - shadow_z_near_) * p);
    float lambda = cascade_linear_factor_;
    levels[i] = (lambda * log_split) + ((1.0f - lambda) * linear_split);
  }

  std::array<glm::mat4, k_max_cascade_levels> light_vp_matrices;
  std::array<glm::mat4, k_max_cascade_levels> light_proj_matrices;
  glm::uvec2 shadow_map_res_{1024, 1024};
  calc_csm_light_space_vp_matrices(std::span(light_vp_matrices.data(), cascade_count_),
                                   std::span(light_proj_matrices.data(), cascade_count_),
                                   std::span(levels.data(), cascade_count_ - 1), cam_view,
                                   light_dir, fov_deg, aspect_ratio, shadow_z_near_, shadow_z_far_,
                                   shadow_map_res_.x);
}

}  // namespace

}  // namespace TENG_NAMESPACE
