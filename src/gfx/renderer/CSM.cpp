#include "CSM.hpp"

#include <cmath>
#include <span>

#include "core/EAssert.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/ShaderManager.hpp"
#include "gfx/renderer/DrawPassSceneBindings.hpp"
#include "gfx/renderer/InstanceMgr.hpp"
#include "gfx/renderer/TaskCmdBufRgIds.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "hlsl/shared_csm.h"

namespace TENG_NAMESPACE::gfx {

using namespace rhi;

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
                              const glm::vec3& light_dir, float shadow_map_res,
                              glm::mat4& light_proj, glm::mat4& light_view) {
  glm::vec3 center{};
  std::array<glm::vec4, 8> corners;
  calc_frustum_corners_world_space(corners, cam_proj * cam_view);
  for (auto v : corners) {
    center += glm::vec3(v);
  }
  center /= 8;

  light_view = glm::lookAt(center + light_dir, center, glm::vec3(0.0f, 1.0f, 0.0f));

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
  // Stabilize shadows by snapping the ortho center to shadow-map texel increments.
  if (shadow_map_res > 0.0f) {
    const float extent_x = max.x - min.x;
    const float extent_y = max.y - min.y;
    const float texel_size_x = extent_x / shadow_map_res;
    const float texel_size_y = extent_y / shadow_map_res;
    glm::vec3 snapped_center = (min + max) * 0.5f;
    snapped_center.x = std::floor(snapped_center.x / texel_size_x) * texel_size_x;
    snapped_center.y = std::floor(snapped_center.y / texel_size_y) * texel_size_y;
    min.x = snapped_center.x - extent_x * 0.5f;
    max.x = snapped_center.x + extent_x * 0.5f;
    min.y = snapped_center.y - extent_y * 0.5f;
    max.y = snapped_center.y + extent_y * 0.5f;
  }

  light_proj = glm::orthoRH_ZO(min.x, max.x, max.y, min.y, min.z, max.z);
  return light_proj * light_view;
}

void calc_csm_light_space_vp_matrices(std::span<glm::mat4> matrices,
                                      std::span<glm::mat4> proj_matrices,
                                      std::span<glm::mat4> light_views, std::span<float> levels,
                                      const glm::mat4& cam_view, const glm::vec3& light_dir,
                                      float fov_deg, float aspect, float cam_near, float cam_far,
                                      std::span<uint32_t> shadow_map_resolutions) {
  ASSERT((matrices.size() && matrices.size() - 1 == levels.size()));
  auto get_proj = [&](float near, float far) {
    return glm::perspectiveRH_ZO(glm::radians(fov_deg), aspect, near, far);
  };

  if (levels.empty()) {
    matrices[0] = calc_light_space_vp(cam_view, get_proj(cam_near, cam_far), light_dir,
                                      shadow_map_resolutions[0], proj_matrices[0], light_views[0]);
  } else {
    matrices[0] = calc_light_space_vp(cam_view, get_proj(cam_near, levels[0]), light_dir,
                                      shadow_map_resolutions[0], proj_matrices[0], light_views[0]);
    for (uint32_t i = 1; i < matrices.size() - 1; i++) {
      matrices[i] =
          calc_light_space_vp(cam_view, get_proj(levels[i - 1], levels[i]), light_dir,
                              shadow_map_resolutions[i], proj_matrices[i], light_views[i]);
    }
    matrices[matrices.size() - 1] =
        calc_light_space_vp(cam_view, get_proj(levels[levels.size() - 1], cam_far), light_dir,
                            shadow_map_resolutions[matrices.size() - 1],
                            proj_matrices[matrices.size() - 1], light_views[matrices.size() - 1]);
  }
}

}  // namespace

void CSMRenderer::bake(std::string_view pass_name, ShadowDepthPassInfo& out,
                       DrawCullPhase cull_phase, const DrawPassSceneBindings& scene,
                       const ViewBindingsMeshlet& view_binds, bool reverse_z) {
  auto& p = rg_.add_graphics_pass(pass_name);
  // No culling for shadows for now
  if (!out.depth_id.is_valid()) {
    out.depth_id =
        rg_.create_texture({.format = TextureFormat::D32float,
                            .dims = {shadow_map_resolutions_[0], shadow_map_resolutions_[0]},
                            .array_layers = cascade_count_,
                            .size_class = SizeClass::Custom},
                           "shadow_depth_tex");
    p.write_depth_output(out.depth_id);
  }

  std::array<RGResourceId, CSM_MAX_CASCADES> out_draw_count_buf_rg_handles;
  std::array<RGResourceId, CSM_MAX_CASCADES> meshlet_draw_stats_rg_handles;
  std::array<TaskCmdBufRgIdsByAlphaMask, CSM_MAX_CASCADES> task_cmd_rg_handles;
  // No 2 pass occlusion culling for shadows for now
  ASSERT(cull_phase == DrawCullPhase::Early);
  DrawCullPhase task_cmd_buf_phase = DrawCullPhase::Early;

  for (uint32_t cascade_i = 0; cascade_i < cascade_count_; cascade_i++) {
    *view_binds.rg_ids[cascade_i].meshlet_draw_stats =
        p.rw_buf(*view_binds.rg_ids[cascade_i].meshlet_draw_stats, PipelineStage::TaskShader);
    meshlet_draw_stats_rg_handles[cascade_i] = *view_binds.rg_ids[cascade_i].meshlet_draw_stats;
    out_draw_count_buf_rg_handles[cascade_i] =
        p.read_buf(*view_binds.rg_ids[cascade_i].draw_count, PipelineStage::TaskShader);
    task_cmd_rg_handles[cascade_i] =
        view_binds.task_cmd_buf_rg_ids[cascade_i]->phase(task_cmd_buf_phase);
  }

  p.set_ex([this, cull_phase, rg_depth = out.depth_id, render_views = view_binds.render_views,
            batch = &scene.draw_batch, materials = scene.materials_buf,
            out_draw_count_buf_rg_handles, meshlet_draw_stats_rg_handles, task_cmd_rg_handles,
            frame_globals = scene.frame_globals_buf_info, reverse_z](CmdEncoder* enc) {
    if (!static_instance_mgr_.has_draws()) {
      return;
    }
    auto depth_handle = rg_.get_att_img(rg_depth);
    ASSERT(depth_handle.is_valid());
    // img views per layer
    if (depth_handle != curr_img_) {
      curr_img_ = depth_handle;
      for (uint32_t cascade_i = 0; cascade_i < cascade_count_; cascade_i++) {
        csm_img_views_[cascade_i] =
            device_->create_tex_view(rg_.get_att_img(rg_depth), 0, 1, cascade_i, 1);
      }
    }
    enc->bind_srv(materials, 11);
    for (uint32_t cascade_i = 0; cascade_i < cascade_count_; cascade_i++) {
      enc->begin_rendering({
          RenderAttInfo::depth_stencil_att(depth_handle, LoadOp::Clear,
                                           {.depth_stencil = {.depth = reverse_z ? 0.f : 1.f}},
                                           rhi::StoreOp::Store, csm_img_views_[cascade_i]),
      });
      const DrawPassSceneBindings mesh_scene{*batch, materials, frame_globals};
      const MeshletMeshPassView mesh_pass{*render_views[cascade_i],
                                          {},
                                          meshlet_draw_stats_rg_handles[cascade_i],
                                          task_cmd_rg_handles[cascade_i],
                                          rg_.get_buf(out_draw_count_buf_rg_handles[cascade_i])};
      encode_meshlet_mesh_draw_pass(
          false, device_, rg_, static_instance_mgr_, enc, cull_phase, false, depth_handle,
          mesh_scene, mesh_pass,
          std::span<const PipelineHandleHolder>(shadow_meshlet_psos_[0],
                                                static_cast<size_t>(AlphaMaskType::Count)));
      enc->end_rendering();
    }
  });
}

CSMRenderer::CSMRenderer(RenderGraph& rg, InstanceMgr& static_instance_mgr, Device* device)
    : static_instance_mgr_(static_instance_mgr), device_(device), rg_(rg) {}

CSMRenderer::~CSMRenderer() = default;

void CSMRenderer::load_pipelines(ShaderManager& shader_mgr) {
  for (size_t alpha_mask_type = 0; alpha_mask_type < AlphaMaskType::Count; alpha_mask_type++) {
    for (size_t late = 0; late < 2; late++) {
      shadow_meshlet_psos_[alpha_mask_type][late] = shader_mgr.create_graphics_pipeline({
          .shaders = {{{late ? "forward_meshlet_late" : "forward_meshlet", ShaderType::Task},
                       {alpha_mask_type == AlphaMaskType::Mask ? "csm_meshlet_alphatest"
                                                               : "csm_meshlet",
                        ShaderType::Mesh},
                       {alpha_mask_type == AlphaMaskType::Mask ? "shadow_depth_meshlet_alphatest"
                                                               : "shadow_depth_meshlet",
                        ShaderType::Fragment}}},
          .rendering = {.depth_format = TextureFormat::D32float},
          .depth_stencil = GraphicsPipelineCreateInfo::depth_enable(true, CompareOp::Less),
      });
    }
  }
}

void CSMRenderer::update(const glm::mat4& cam_view, glm::vec3 cam_pos, glm::vec3 light_dir,
                         float fov_deg, float aspect_ratio) {
  (void)cam_pos;
  glm::vec3 light_dir_ws = -glm::normalize(light_dir);
  float shadow_z_near_ = 0.1f;
  float shadow_z_far_ = 200.0f;
  float cascade_linear_factor_ = 0.95f;
  csm_data_.num_cascades = cascade_count_;
  csm_data_.biases = glm::vec4(0.0004f, 0.0025f, 0.0f, shadow_z_far_);
  csm_data_.cascade_levels = glm::vec4(shadow_z_far_);

  for (uint32_t i = 0; i < cascade_count_ - 1; i++) {
    float p = (i + 1) / static_cast<float>(cascade_count_);
    float log_split = shadow_z_near_ * std::pow(shadow_z_far_ / shadow_z_near_, p);
    float linear_split = shadow_z_near_ + ((shadow_z_far_ - shadow_z_near_) * p);
    float lambda = cascade_linear_factor_;
    csm_data_.cascade_levels[i] = (lambda * log_split) + ((1.0f - lambda) * linear_split);
  }

  calc_csm_light_space_vp_matrices(std::span(csm_data_.light_vp_matrices, cascade_count_),
                                   std::span(light_proj_matrices_.data(), cascade_count_),
                                   std::span(light_views_.data(), cascade_count_),
                                   std::span(&csm_data_.cascade_levels.x, cascade_count_ - 1),
                                   cam_view, light_dir_ws, fov_deg, aspect_ratio, shadow_z_near_,
                                   shadow_z_far_, shadow_map_resolutions_);
}

}  // namespace TENG_NAMESPACE::gfx
