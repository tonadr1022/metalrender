#include "MeshletCsmRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec4.hpp>
#include <limits>
#include <span>
#include <string>

#include "MeshletTestRenderUtil.hpp"
#include "gfx/GPUFrameAllocator2.hpp"
#include "gfx/ImGuiRenderer.hpp"
#include "gfx/ModelGPUManager.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/ShaderManager.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/Device.hpp"
#include "hlsl/shared_forward_meshlet.h"
#include "imgui.h"

namespace teng::gfx {

using namespace rhi;

namespace {

void calc_frustum_corners_world_space(std::span<glm::vec4> corners, const glm::mat4& vp_matrix) {
  const glm::mat4 inv_vp = glm::inverse(vp_matrix);
  for (uint32_t z = 0, i = 0; z < 2; z++) {
    for (uint32_t y = 0; y < 2; y++) {
      for (uint32_t x = 0; x < 2; x++, i++) {
        const glm::vec4 pt =
            inv_vp * glm::vec4((2.f * x) - 1.f, (2.f * y) - 1.f, static_cast<float>(z), 1.f);
        corners[i] = pt / pt.w;
      }
    }
  }
}

glm::mat4 calc_light_space_vp(const glm::mat4& cam_view, const glm::mat4& cam_proj,
                              const glm::vec3& light_dir, float shadow_map_res,
                              float min_light_depth_padding, glm::mat4& light_proj,
                              glm::mat4& light_view, glm::vec3& light_min, glm::vec3& light_max) {
  glm::vec3 center{0.f};
  std::array<glm::vec4, 8> corners;
  calc_frustum_corners_world_space(corners, cam_proj * cam_view);
  for (auto v : corners) {
    center += glm::vec3(v);
  }
  center /= 8.f;

  light_view = glm::lookAt(center + light_dir, center, glm::vec3(0.f, 1.f, 0.f));
  glm::vec3 min{std::numeric_limits<float>::max()};
  glm::vec3 max{std::numeric_limits<float>::lowest()};
  for (auto corner : corners) {
    const glm::vec3 c = light_view * corner;
    min.x = glm::min(min.x, c.x);
    max.x = glm::max(max.x, c.x);
    min.y = glm::min(min.y, c.y);
    max.y = glm::max(max.y, c.y);
    min.z = glm::min(min.z, c.z);
    max.z = glm::max(max.z, c.z);
  }

  if (shadow_map_res > 0.0f) {
    const float extent_x = max.x - min.x;
    const float extent_y = max.y - min.y;
    if (extent_x > 0.f && extent_y > 0.f) {
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
  }

  const float relative_z_padding = (max.z - min.z) * 1.5f;
  const float z_padding = std::max(relative_z_padding, min_light_depth_padding);
  min.z -= z_padding;
  max.z += z_padding;
  light_min = min;
  light_max = max;
  light_proj = glm::orthoRH_ZO(min.x, max.x, max.y, min.y, min.z, max.z);
  return light_proj * light_view;
}

CullData prepare_ortho_cull_data(const glm::vec3& min, const glm::vec3& max) {
  CullData cd{};
  cd.ortho_bounds = glm::vec4(min.x, max.x, min.y, max.y);
  cd.z_near = min.z;
  cd.z_far = max.z;
  cd.projection_type = CULL_PROJECTION_ORTHOGRAPHIC;
  return cd;
}

}  // namespace

MeshletCsmRenderer::MeshletCsmRenderer(rhi::Device& device, RenderGraph& rg,
                                       ModelGPUMgr& model_gpu_mgr, ShaderManager& shader_mgr)
    : device_(device), rg_(rg), model_gpu_mgr_(model_gpu_mgr) {
  for (size_t a = 0; a < static_cast<size_t>(AlphaMaskType::Count); ++a) {
    shadow_psos_[a] = shader_mgr.create_graphics_pipeline({
        .shaders = {{{"forward_meshlet", ShaderType::Task},
                     {a == static_cast<size_t>(AlphaMaskType::Mask)
                          ? "meshlet_test/csm_meshlet_alphatest"
                          : "meshlet_test/csm_meshlet",
                      ShaderType::Mesh},
                     {a == static_cast<size_t>(AlphaMaskType::Mask)
                          ? "meshlet_test/shadow_depth_meshlet_alphatest"
                          : "meshlet_test/shadow_depth_meshlet",
                      ShaderType::Fragment}}},
        .rendering = {.depth_format = TextureFormat::D32float},
        .depth_stencil = GraphicsPipelineCreateInfo::depth_enable(true, CompareOp::Less),
        .name = std::string("meshlet_test_shadow_") + std::to_string(a),
    });
  }
}

void MeshletCsmRenderer::set_scene_defaults(float z_near, float z_far, uint32_t cascade_count,
                                            float split_lambda) {
  constexpr float k_min_z_near = 1e-4f;
  const float near_v = std::max(z_near, k_min_z_near);
  const float far_v = std::max(z_far, near_v + k_min_z_near);

  cfg_.z_near = near_v;
  cfg_.z_far = far_v;
  cfg_.cascade_count = std::clamp(cascade_count, 1u, cfg_.max_cascades);
  cfg_.split_lambda = std::clamp(split_lambda, 0.0f, 1.0f);
}

void MeshletCsmRenderer::shutdown() {
  destroy_layer_views();
  cached_shadow_depth_tex_ = {};
}

void MeshletCsmRenderer::destroy_layer_views() {
  if (!cached_shadow_depth_tex_.is_valid()) {
    return;
  }
  for (auto& view : shadow_depth_layer_views_) {
    if (view >= 0) {
      device_.destroy(cached_shadow_depth_tex_, view);
      view = -1;
    }
  }
}

void MeshletCsmRenderer::ensure_layer_views(rhi::TextureHandle depth_img) {
  if (depth_img == cached_shadow_depth_tex_) {
    return;
  }

  destroy_layer_views();
  cached_shadow_depth_tex_ = depth_img;
  for (uint32_t cascade_i = 0; cascade_i < cfg_.max_cascades; cascade_i++) {
    shadow_depth_layer_views_[cascade_i] = device_.create_tex_view(depth_img, 0, 1, cascade_i, 1);
  }
}

void MeshletCsmRenderer::on_imgui() {
  bool enabled = mode_ == MeshletShadowMode::CascadedShadowMaps;
  if (ImGui::Checkbox("CSM shadows", &enabled)) {
    mode_ = enabled ? MeshletShadowMode::CascadedShadowMaps : MeshletShadowMode::None;
  }

  ImGui::BeginDisabled(!enabled);
  int shadow_cascade_count = static_cast<int>(cfg_.cascade_count);
  if (ImGui::SliderInt("Shadow cascades", &shadow_cascade_count, 1,
                       static_cast<int>(cfg_.max_cascades))) {
    cfg_.cascade_count = static_cast<uint32_t>(
        std::clamp(shadow_cascade_count, 1, static_cast<int>(cfg_.max_cascades)));
  }
  ImGui::SliderFloat("Shadow split lambda", &cfg_.split_lambda, 0.0f, 1.0f);
  ImGui::DragFloat("Shadow depth padding", &cfg_.min_light_depth_padding, 0.1f, 0.0f, 1000.0f,
                   "%.1f");
  cfg_.min_light_depth_padding = std::max(cfg_.min_light_depth_padding, 0.f);
  ImGui::DragFloat("Shadow z near", &cfg_.z_near, 1.0f, 0.0f, 10000.0f, "%.1f");
  cfg_.z_near = std::max(cfg_.z_near, 0.0001f);
  ImGui::DragFloat("Shadow z far", &cfg_.z_far, 1.0f, 0.0f, 10000.0f, "%.1f");
  cfg_.z_far = std::max(cfg_.z_far, cfg_.z_near + 0.0001f);
  ImGui::SliderFloat("Shadow bias min", &cfg_.bias_min, 0.0f, 0.01f, "%.5f");
  ImGui::SliderFloat("Shadow bias max", &cfg_.bias_max, 0.0f, 0.02f, "%.5f");
  ImGui::Checkbox("Visualize shadow cascades", &visualize_shadow_cascades_);
  ImGui::EndDisabled();

  ImGui::SeparatorText("CSM debug");
  ImGui::BeginDisabled(!enabled);
  const int last_cascade = static_cast<int>(cfg_.cascade_count) - 1;
  if (last_cascade >= 0) {
    debug_csm_cascade_layer_ = std::clamp(debug_csm_cascade_layer_, 0, last_cascade);
  }
  ImGui::SliderInt("View cascade (depth)", &debug_csm_cascade_layer_, 0, std::max(0, last_cascade));
  if (enabled && cached_shadow_depth_tex_.is_valid() && cfg_.cascade_count > 0) {
    const uint32_t array_b = device_.get_tex(cached_shadow_depth_tex_)->bindless_idx();
    const uint32_t layer = static_cast<uint32_t>(std::max(0, debug_csm_cascade_layer_));
    const float disp_w = 240.f;
    const float disp_h = disp_w;
    ImGui::Image(MakeImGuiTexRefCsmArraySlice(array_b, layer), ImVec2(disp_w, disp_h), ImVec2(0, 0),
                 ImVec2(1, 1));
  } else {
    ImGui::TextUnformatted("CSM depth preview (not ready or CSM disabled)");
  }
  ImGui::EndDisabled();
}

MeshletCsmRenderer::FrameData MeshletCsmRenderer::build_frame_data(
    const ViewData& camera_view, const glm::vec3& toward_light) const {
  const uint32_t cascade_count = cfg_.cascade_count;
  FrameData out{};
  out.cascade_count = cascade_count;
  out.csm_data.num_cascades = cascade_count;
  out.csm_data.biases = glm::vec4(cfg_.bias_min, cfg_.bias_max, 0.0f, cfg_.z_far);
  out.csm_data.cascade_levels = glm::vec4(cfg_.z_far);

  const glm::vec3 light_dir_ws = glm::normalize(toward_light);
  constexpr float k_fov_deg = 60.f;
  const float aspect =
      static_cast<float>(camera_view.proj[1][1]) / std::max(1e-6f, camera_view.proj[0][0]);

  for (uint32_t i = 0; i + 1 < cascade_count; i++) {
    const float p = (i + 1) / static_cast<float>(cascade_count);
    const float log_split = cfg_.z_near * std::pow(cfg_.z_far / cfg_.z_near, p);
    const float linear_split = cfg_.z_near + ((cfg_.z_far - cfg_.z_near) * p);
    out.csm_data.cascade_levels[i] =
        (cfg_.split_lambda * log_split) + ((1.0f - cfg_.split_lambda) * linear_split);
  }

  auto get_proj = [&](float near_z, float far_z) {
    return glm::perspectiveRH_ZO(glm::radians(k_fov_deg), aspect, near_z, far_z);
  };

  for (uint32_t cascade_i = 0; cascade_i < cascade_count; cascade_i++) {
    const float split_near =
        (cascade_i == 0) ? cfg_.z_near : out.csm_data.cascade_levels[cascade_i - 1];
    const float split_far =
        (cascade_i + 1 == cascade_count) ? cfg_.z_far : out.csm_data.cascade_levels[cascade_i];
    glm::mat4 light_proj{};
    glm::mat4 light_view{};
    glm::vec3 light_min{};
    glm::vec3 light_max{};
    const glm::mat4 light_vp = calc_light_space_vp(
        camera_view.view, get_proj(split_near, split_far), light_dir_ws,
        static_cast<float>(cfg_.shadow_map_resolution), cfg_.min_light_depth_padding, light_proj,
        light_view, light_min, light_max);
    out.csm_data.light_vp_matrices[cascade_i] = light_vp;
    ViewData& cascade_vd = out.view_data[cascade_i];
    cascade_vd.vp = light_vp;
    cascade_vd.inv_vp = glm::inverse(light_vp);
    cascade_vd.view = light_view;
    cascade_vd.proj = light_proj;
    cascade_vd.inv_proj = glm::inverse(light_proj);
    cascade_vd.camera_pos = glm::vec4(0.f, 0.f, 0.f, 1.f);
    out.cull_data[cascade_i] = prepare_ortho_cull_data(light_min, light_max);
  }

  return out;
}

MeshletCsmRenderer::Output MeshletCsmRenderer::bake(const BakeRequest& req) {
  if (!enabled()) {
    CSMData disabled_csm{};
    disabled_csm.num_cascades = 0;
    return {
        .mode = MeshletShadowMode::None,
        .valid = false,
        .csm_cb = req.frame_uniform_allocator.alloc2(sizeof(CSMData), &disabled_csm),
    };
  }

  FrameData frame = build_frame_data(req.camera_view, req.toward_light);
  const BufferSuballoc csm_cb =
      req.frame_uniform_allocator.alloc2(sizeof(CSMData), &frame.csm_data);

  std::array<BufferSuballoc, CSM_MAX_CASCADES> view_cbs{};
  std::array<BufferSuballoc, CSM_MAX_CASCADES> cull_cbs{};
  for (uint32_t cascade_i = 0; cascade_i < frame.cascade_count; cascade_i++) {
    view_cbs[cascade_i] =
        req.frame_uniform_allocator.alloc2(sizeof(ViewData), &frame.view_data[cascade_i]);
    cull_cbs[cascade_i] =
        req.frame_uniform_allocator.alloc2(sizeof(CullData), &frame.cull_data[cascade_i]);
  }

  std::array<MeshletDrawPrep::PassBuffers, CSM_MAX_CASCADES> cascade_draws{};
  std::array<RGResourceId, CSM_MAX_CASCADES> indirect_args{};
  RGResourceId visible_count =
      req.draw_prep.create_visible_count_buffer("meshlet_shadow_visible_object_count");
  req.draw_prep.clear_visible_count(visible_count, "meshlet_clear_shadow_visible_count");

  for (uint32_t cascade_i = 0; cascade_i < frame.cascade_count; cascade_i++) {
    cascade_draws[cascade_i] = req.draw_prep.create_pass_buffers(
        "meshlet_shadow_" + std::to_string(cascade_i), req.task_cmd_count, visible_count);
    indirect_args[cascade_i] = cascade_draws[cascade_i].indirect_args_rg;
  }
  req.draw_prep.clear_indirect_args(
      "meshlet_clear_shadow_indirect_mesh_cmds",
      std::span(indirect_args.data(), static_cast<size_t>(frame.cascade_count)));
  for (uint32_t cascade_i = 0; cascade_i < frame.cascade_count; cascade_i++) {
    cascade_draws[cascade_i].indirect_args_rg = indirect_args[cascade_i];
    req.draw_prep.bake_task_commands(
        {
            .pass_name = "meshlet_prepare_shadow_" + std::to_string(cascade_i),
            .max_draws = req.max_draws,
            .late = false,
            .object_frustum_cull = false,
            .object_occlusion_cull = false,
            .view_cb = view_cbs[cascade_i],
            .cull_cb = cull_cbs[cascade_i],
        },
        cascade_draws[cascade_i]);
    visible_count = cascade_draws[cascade_i].visible_object_count_rg;
  }

  const RGResourceId shadow_depth =
      rg_.create_texture({.format = TextureFormat::D32float,
                          .dims = {cfg_.shadow_map_resolution, cfg_.shadow_map_resolution},
                          .array_layers = cfg_.max_cascades,
                          .size_class = SizeClass::Custom},
                         "meshlet_shadow_depth_att");
  RGResourceId shadow_depth_id{};
  auto& p = rg_.add_graphics_pass("meshlet_shadow_cascades");
  for (uint32_t cascade_i = 0; cascade_i < frame.cascade_count; cascade_i++) {
    cascade_draws[cascade_i].task_cmd_rg =
        p.read_buf(cascade_draws[cascade_i].task_cmd_rg,
                   PipelineStage::MeshShader | PipelineStage::TaskShader);
    cascade_draws[cascade_i].indirect_args_rg = p.read_buf(
        cascade_draws[cascade_i].indirect_args_rg,
        PipelineStage::TaskShader | PipelineStage::DrawIndirect, AccessFlags::IndirectCommandRead);
  }
  req.meshlet_vis_rg = p.rw_buf(req.meshlet_vis_rg, PipelineStage::TaskShader);
  req.meshlet_stats_rg = p.rw_buf(req.meshlet_stats_rg, PipelineStage::TaskShader);
  shadow_depth_id = p.write_depth_output(shadow_depth);

  const auto local_draws = cascade_draws;
  const auto local_view_cbs = view_cbs;
  const auto local_cull_cbs = cull_cbs;
  p.set_ex([this, shadow_depth_id, local_draws, local_view_cbs, local_cull_cbs,
            meshlet_vis_rg = req.meshlet_vis_rg, meshlet_stats_rg = req.meshlet_stats_rg,
            cascade_count = frame.cascade_count,
            shadow_globals_cb = req.shadow_globals_cb](CmdEncoder* enc) {
    auto& geo_batch = model_gpu_mgr_.geometry_batch();
    const rhi::TextureHandle depth_img = rg_.get_att_img(shadow_depth_id);
    ensure_layer_views(depth_img);

    const glm::uvec2 shadow_vp_dims{cfg_.shadow_map_resolution, cfg_.shadow_map_resolution};
    constexpr uint32_t shadow_meshlet_flags = MESHLET_FRUSTUM_CULL_ENABLED_BIT;
    for (uint32_t cascade_i = 0; cascade_i < cascade_count; cascade_i++) {
      enc->begin_rendering({
          RenderAttInfo::depth_stencil_att(
              depth_img, LoadOp::Clear, ClearValue{.depth_stencil = {.depth = 1.f, .stencil = 0}},
              rhi::StoreOp::Store, shadow_depth_layer_views_[cascade_i]),
      });
      encode_meshlet_test_draw_pass(
          false, false, shadow_meshlet_flags, &device_, rg_, geo_batch,
          model_gpu_mgr_.materials_allocator().get_buffer_handle(), shadow_globals_cb,
          local_view_cbs[cascade_i], local_cull_cbs[cascade_i], rhi::TextureHandle{},
          shadow_vp_dims, meshlet_vis_rg, meshlet_stats_rg, local_draws[cascade_i].task_cmd_rg,
          rg_.get_buf(local_draws[cascade_i].indirect_args_rg), model_gpu_mgr_.instance_mgr(),
          std::span(shadow_psos_), enc);
      enc->end_rendering();
    }
  });

  return {
      .mode = MeshletShadowMode::CascadedShadowMaps,
      .valid = true,
      .depth_rg = shadow_depth_id,
      .csm_cb = csm_cb,
      .cascade_count = frame.cascade_count,
      .sample_layer_count = cfg_.max_cascades,
  };
}

}  // namespace teng::gfx
