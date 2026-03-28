#include "CSM.hpp"

#include <span>

#include "core/EAssert.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/ShaderManager.hpp"
#include "gfx/renderer/DrawPassSceneBindings.hpp"
#include "gfx/renderer/InstanceMgr.hpp"
#include "gfx/renderer/TaskCmdBufRgIds.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
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

glm::mat4 calc_light_space_vp(const glm::mat4& cam_view, const glm::mat4& light_view,
                              const glm::mat4& cam_proj, float, glm::mat4& light_proj) {
  glm::vec3 center;
  std::array<glm::vec4, 8> corners;
  calc_frustum_corners_world_space(corners, cam_proj * cam_view);
  for (auto v : corners) {
    center += glm::vec3(v);
  }
  center /= 8;

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
                                      const glm::mat4& cam_view, const glm::mat4& light_view,
                                      float fov_deg, float aspect, float cam_near, float cam_far,
                                      uint32_t shadow_map_res) {
  ASSERT((matrices.size() && matrices.size() - 1 == levels.size()));
  auto get_proj = [&](float near, float far) {
    auto mat = glm::perspective(glm::radians(fov_deg), aspect, near, far);
    mat[1][1] *= -1;
    return mat;
  };

  if (levels.empty()) {
    matrices[0] = calc_light_space_vp(cam_view, light_view, get_proj(cam_near, cam_far),
                                      shadow_map_res, proj_matrices[0]);
  } else {
    matrices[0] = calc_light_space_vp(cam_view, light_view, get_proj(cam_near, levels[0]),
                                      shadow_map_res, proj_matrices[0]);
    for (uint32_t i = 1; i < matrices.size() - 1; i++) {
      matrices[i] = calc_light_space_vp(cam_view, light_view, get_proj(levels[i - 1], levels[i]),
                                        shadow_map_res, proj_matrices[i]);
    }
    matrices[matrices.size() - 1] =
        calc_light_space_vp(cam_view, light_view, get_proj(levels[levels.size() - 1], cam_far),
                            shadow_map_res, proj_matrices[matrices.size() - 1]);
  }
}

[[maybe_unused]] void calc_csm_data(CSMData& data, std::span<glm::mat4> light_proj_matrices,
                                    const glm::mat4& cam_view, const glm::mat4& light_view,
                                    uint32_t cascade_count) {
  float shadow_z_near_ = 0.1f;
  float shadow_z_far_ = 100.0f;
  float cascade_linear_factor_ = 0.95f;
  float fov_deg = 90.0f;
  // TODO: no
  float aspect_ratio = 1.0f;
  for (uint32_t i = 0; i < cascade_count - 1; i++) {
    float p = (i + 1) / static_cast<float>(cascade_count);
    float log_split = shadow_z_near_ * std::pow(shadow_z_far_ / shadow_z_near_, p);
    float linear_split = shadow_z_near_ + ((shadow_z_far_ - shadow_z_near_) * p);
    float lambda = cascade_linear_factor_;
    data.cascade_levels[i] = (lambda * log_split) + ((1.0f - lambda) * linear_split);
  }

  glm::uvec2 shadow_map_res_{1024, 1024};
  calc_csm_light_space_vp_matrices(std::span(data.light_vp_matrices, cascade_count),
                                   std::span(light_proj_matrices.data(), cascade_count),
                                   std::span(&data.cascade_levels.x, cascade_count - 1), cam_view,
                                   light_view, fov_deg, aspect_ratio, shadow_z_near_, shadow_z_far_,
                                   shadow_map_res_.x);
}

}  // namespace

void CSMRenderer::bake(ShadowDepthPassInfo& out, DrawCullPhase cull_phase,
                       const DrawPassSceneBindings& scene, const ViewBindingsMeshlet& view,
                       bool reverse_z) {
  auto& p = rg_.add_graphics_pass("csm");
  // No culling for shadows for now
  out.depth_id = rg_.create_texture({.format = TextureFormat::D32float}, "shadow_depth_tex");
  p.write_depth_output(out.depth_id);

  view.rg_ids.meshlet_draw_stats =
      p.rw_buf(view.rg_ids.meshlet_draw_stats, PipelineStage::TaskShader);

  RGResourceId meshlet_stats_for_pass = view.rg_ids.meshlet_draw_stats;

  ASSERT(cull_phase == DrawCullPhase::Early);
  DrawCullPhase task_cmd_buf_phase = DrawCullPhase::Early;

  RGResourceId out_draw_count_buf_rg_handle =
      p.read_buf(view.rg_ids.draw_count, PipelineStage::TaskShader);
  p.set_ex([this, cull_phase, rg_depth = out.depth_id, rv = &view.render_view,
            batch = &scene.draw_batch, materials = scene.materials_buf,
            meshlet_stats_rg = meshlet_stats_for_pass, frame_globals = scene.frame_globals_buf_info,
            out_draw_count_rg = out_draw_count_buf_rg_handle,
            task_cmd_rg = view.task_cmd_buf_rg_ids.phase(task_cmd_buf_phase),
            reverse_z](CmdEncoder* enc) {
    if (!static_instance_mgr_.has_draws()) {
      return;
    }
    auto depth_handle = rg_.get_att_img(rg_depth);
    ASSERT(depth_handle.is_valid());
    auto load_op = LoadOp::Clear;
    enc->begin_rendering({
        RenderAttInfo::depth_stencil_att(depth_handle, load_op,
                                         {.depth_stencil = {.depth = reverse_z ? 0.f : 1.f}}),
    });
    enc->bind_srv(materials, 11);

    const DrawPassSceneBindings mesh_scene{*batch, materials, frame_globals};
    const MeshletMeshPassView mesh_pass{
        *rv, {}, meshlet_stats_rg, task_cmd_rg, rg_.get_buf(out_draw_count_rg)};
    encode_meshlet_mesh_draw_pass(
        reverse_z, device_, rg_, static_instance_mgr_, enc, cull_phase, false, depth_handle,
        mesh_scene, mesh_pass,
        std::span<const PipelineHandleHolder>(shadow_meshlet_psos_,
                                              static_cast<size_t>(AlphaMaskType::Count)));
    enc->end_rendering();
  });
}

CSMRenderer::CSMRenderer(RenderGraph& rg, InstanceMgr& static_instance_mgr, Device* device)
    : static_instance_mgr_(static_instance_mgr), device_(device), rg_(rg) {}

CSMRenderer::~CSMRenderer() = default;

void CSMRenderer::load_pipelines(ShaderManager& shader_mgr) {
  for (size_t alpha_mask_type = 0; alpha_mask_type < AlphaMaskType::Count; alpha_mask_type++) {
    shadow_meshlet_psos_[alpha_mask_type] = shader_mgr.create_graphics_pipeline({
        .shaders = {{{"forward_meshlet", ShaderType::Task},
                     {"csm_meshlet", ShaderType::Mesh},
                     {alpha_mask_type == AlphaMaskType::Mask ? "shadow_depth_meshlet_alphatest"
                                                             : "shadow_depth_meshlet",
                      ShaderType::Fragment}}},
        .rendering = {.depth_format = TextureFormat::D32float},
        .depth_stencil = GraphicsPipelineCreateInfo::depth_enable(true, CompareOp::Less),
    });
  }
}

void CSMRenderer::update(const glm::mat4& cam_view, glm::vec3 cam_pos, glm::vec3 light_dir) {
  glm::mat4 light_view = glm::lookAt(cam_pos, cam_pos + light_dir, glm::vec3(0.0f, 1.0f, 0.0f));
  light_view_ = light_view;
  calc_csm_data(csn_data_, light_proj_matrices_, cam_view, light_view, cascade_count_);
}

}  // namespace TENG_NAMESPACE::gfx