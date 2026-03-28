#pragma once

#include <span>

#include "core/Config.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/renderer/AlphaMaskType.hpp"
#include "gfx/renderer/DrawPassSceneBindings.hpp"
#include "gfx/renderer/TaskCmdBufRgIds.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "hlsl/shared_csm.h"

namespace TENG_NAMESPACE {

namespace gfx {

namespace rhi {

class Device;

}  // namespace rhi

class RenderGraph;
class ShaderManager;
class InstanceMgr;
struct DrawPassSceneBindings;
struct RenderView;

void calc_csm_light_space_vp_matrices(std::span<glm::mat4> matrices,
                                      std::span<glm::mat4> proj_matrices, std::span<float> levels,
                                      const glm::mat4& cam_view, glm::vec3 light_dir, float fov_deg,
                                      float aspect, float cam_near, float cam_far,
                                      uint32_t shadow_map_res);

class CSMRenderer {
 public:
  struct ViewBindingsMeshlet {
    std::array<TaskCmdBufRgIdsPerView*, CSM_MAX_CASCADES> task_cmd_buf_rg_ids;
    std::array<RenderView*, CSM_MAX_CASCADES> render_views;
    std::array<ViewRgIds, CSM_MAX_CASCADES> rg_ids;
  };
  CSMRenderer(RenderGraph& rg, InstanceMgr& static_instance_mgr, rhi::Device* device);
  ~CSMRenderer();
  struct ShadowDepthPassInfo {
    // TODO: rename to depth_tex_id
    RGResourceId depth_id{};
  };
  void update(const glm::mat4& cam_view, glm::vec3 cam_pos, glm::vec3 light_dir);
  void bake(std::string_view pass_name, ShadowDepthPassInfo& out, DrawCullPhase cull_phase,
            const DrawPassSceneBindings& scene, const ViewBindingsMeshlet& view, bool reverse_z);
  void load_pipelines(ShaderManager& shader_mgr);
  [[nodiscard]] uint32_t num_cascades() const { return cascade_count_; }

  [[nodiscard]] const CSMData& get_csm_data() const { return csm_data_; }
  [[nodiscard]] const glm::mat4& get_light_proj(uint32_t cascade_idx) const {
    return light_proj_matrices_[cascade_idx];
  }
  [[nodiscard]] const glm::mat4& get_light_view() const { return light_view_; }

 private:
  CSMData csm_data_;
  std::array<glm::mat4, CSM_MAX_CASCADES> light_proj_matrices_;
  std::array<int32_t, CSM_MAX_CASCADES> csm_img_views_{-1, -1, -1, -1};
  rhi::TextureHandle curr_img_;
  glm::mat4 light_view_{};
  rhi::PipelineHandleHolder shadow_meshlet_psos_[(size_t)AlphaMaskType::Count];
  InstanceMgr& static_instance_mgr_;
  rhi::Device* device_;
  RenderGraph& rg_;

  uint32_t shadow_map_resolutions_[CSM_MAX_CASCADES] = {1024, 1024, 1024, 1024};
  uint32_t cascade_count_{3};
};

}  // namespace gfx

}  // namespace TENG_NAMESPACE
