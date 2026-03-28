#pragma once

#include <span>

#include "core/Config.hpp"
#include "glm/ext/matrix_float4x4.hpp"

namespace TENG_NAMESPACE {

namespace gfx {

namespace rhi {

class Device;

}  // namespace rhi

class RenderGraph;
class ShaderManager;

void calc_csm_light_space_vp_matrices(std::span<glm::mat4> matrices,
                                      std::span<glm::mat4> proj_matrices, std::span<float> levels,
                                      const glm::mat4& cam_view, glm::vec3 light_dir, float fov_deg,
                                      float aspect, float cam_near, float cam_far,
                                      uint32_t shadow_map_res);

class CSMRenderer {
 public:
  CSMRenderer(RenderGraph& rg, ShaderManager& shader_mgr, rhi::Device* device);
  void bake();

 private:
  rhi::Device* device_;
  RenderGraph& rg_;
  ShaderManager& shader_mgr_;
};

}  // namespace gfx

}  // namespace TENG_NAMESPACE
