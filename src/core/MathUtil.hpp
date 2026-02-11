#pragma once

#include <cstring>

#include "core/Config.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/vec3.hpp"

namespace TENG_NAMESPACE {

namespace math {
// Source:
// https://github.com/zeux/niagara/blob/7fa51801abc258c3cb05e9a615091224f02e11cf/src/scene.cpp#L282
inline void decompose_matrix(const float *matrix, glm::vec3 &translation, glm::quat &rotation,
                             glm::vec3 &scale) {
  float m[4][4];
  memcpy(m, matrix, sizeof(float) * 16);

  // extract translation from last row
  translation[0] = m[3][0];
  translation[1] = m[3][1];
  translation[2] = m[3][2];

  // compute determinant to determine handedness
  const float det = m[0][0] * (m[1][1] * m[2][2] - m[2][1] * m[1][2]) -
                    m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                    m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);

  const float sign = (det < 0.f) ? -1.f : 1.f;

  // recover scale from axis lengths
  scale[0] = sqrtf(m[0][0] * m[0][0] + m[0][1] * m[0][1] + m[0][2] * m[0][2]) * sign;
  scale[1] = sqrtf(m[1][0] * m[1][0] + m[1][1] * m[1][1] + m[1][2] * m[1][2]) * sign;
  scale[2] = sqrtf(m[2][0] * m[2][0] + m[2][1] * m[2][1] + m[2][2] * m[2][2]) * sign;

  // normalize axes to get a pure rotation matrix
  const float rsx = (scale[0] == 0.f) ? 0.f : 1.f / scale[0];
  const float rsy = (scale[1] == 0.f) ? 0.f : 1.f / scale[1];
  const float rsz = (scale[2] == 0.f) ? 0.f : 1.f / scale[2];

  const float r00 = m[0][0] * rsx, r10 = m[1][0] * rsy, r20 = m[2][0] * rsz;
  const float r01 = m[0][1] * rsx, r11 = m[1][1] * rsy, r21 = m[2][1] * rsz;
  const float r02 = m[0][2] * rsx, r12 = m[1][2] * rsy, r22 = m[2][2] * rsz;

  const int qc = r22 < 0 ? (r00 > r11 ? 0 : 1) : (r00 < -r11 ? 2 : 3);
  const float qs1 = qc & 2 ? -1.f : 1.f;
  const float qs2 = qc & 1 ? -1.f : 1.f;
  const float qs3 = (qc - 1) & 2 ? -1.f : 1.f;

  const float qt = 1.f - qs3 * r00 - qs2 * r11 - qs1 * r22;
  const float qs = 0.5f / sqrtf(qt);

  rotation[qc ^ 0] = qs * qt;
  rotation[qc ^ 1] = qs * (r01 + qs1 * r10);
  rotation[qc ^ 2] = qs * (r20 + qs2 * r02);
  rotation[qc ^ 3] = qs * (r12 + qs3 * r21);
}

inline size_t get_mip_levels(size_t w, size_t h) {
  return std::floor(std::log2(std::max(w, h))) + 1;
}

}  // namespace math

}  // namespace TENG_NAMESPACE
