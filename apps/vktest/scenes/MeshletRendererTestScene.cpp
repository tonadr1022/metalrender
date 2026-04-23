#include "MeshletRendererTestScene.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <numbers>
#include <span>
#include <string>

#include "ResourceManager.hpp"
#include "Window.hpp"
#include "core/MathUtil.hpp"
#include "core/Util.hpp"
#include "gfx/DrawBatch.hpp"
#include "gfx/ImGuiRenderer.hpp"
#include "gfx/ModelGPUManager.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/ShaderManager.hpp"
#include "gfx/renderer/TaskCmdBufRgIds.hpp"
#include "gfx/rhi/Swapchain.hpp"
#include "gfx/rhi/Texture.hpp"
#include "hlsl/depth_reduce/shared_depth_reduce.h"
#include "hlsl/meshlet_test/shared_meshlet_test_shade.h"
#include "hlsl/shader_constants.h"
#include "hlsl/shared_csm.h"
#include "hlsl/shared_debug_meshlet_prepare.h"
#include "hlsl/shared_forward_meshlet.h"
#include "hlsl/shared_globals.h"
#include "hlsl/shared_meshlet_draw_stats.hlsli"
#include "hlsl/shared_task_cmd.h"
#include "hlsl/shared_test_clear_buf.h"
#include "imgui.h"

namespace teng::gfx {

using namespace teng::demo_scenes;
using namespace rhi;

namespace {

glm::mat4 infinite_perspective_proj(float fov_y, float aspect, float z_near) {
  // clang-format off
  float f = 1.0f / tanf(fov_y / 2.0f);
  return {
    f / aspect, 0.0f, 0.0f, 0.0f,
    0.0f, f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, -1.0f,
    0.0f, 0.0f, z_near, 0.0f};
  // clang-format on
}

uint32_t prev_pow2(uint32_t val) {
  uint32_t v = 1;
  while (v * 2 < val) {
    v *= 2;
  }
  return v;
}

void calc_frustum_corners_world_space(std::span<glm::vec4> corners, const glm::mat4& vp_matrix) {
  const glm::mat4 inv_vp = glm::inverse(vp_matrix);
  for (uint32_t z = 0, i = 0; z < 2; z++) {
    for (uint32_t y = 0; y < 2; y++) {
      for (uint32_t x = 0; x < 2; x++, i++) {
        glm::vec4 pt =
            inv_vp * glm::vec4((2.f * x) - 1.f, (2.f * y) - 1.f, static_cast<float>(z), 1.f);
        corners[i] = pt / pt.w;
      }
    }
  }
}

glm::mat4 calc_light_space_vp(const glm::mat4& cam_view, const glm::mat4& cam_proj,
                              const glm::vec3& light_dir, float shadow_map_res,
                              glm::mat4& light_proj, glm::mat4& light_view) {
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

  const float z_padding = (max.z - min.z) * 1.5f;
  min.z -= z_padding;
  max.z += z_padding;

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

void add_buffer_readback_copy2(RenderGraph& rg, std::string_view pass_name, RGResourceId& src_buf,
                               RGResourceId dst_rg_id, size_t src_offset, size_t dst_offset,
                               size_t size_bytes) {
  auto& p = rg.add_transfer_pass(pass_name);
  auto old_src_buf = src_buf;
  src_buf = p.copy_from_buf(old_src_buf);
  p.write_buf(dst_rg_id, rhi::PipelineStage::AllTransfer);
  p.set_ex([&rg, src_buf, dst_rg_id, dst_offset, src_offset, size_bytes](rhi::CmdEncoder* enc) {
    enc->copy_buffer_to_buffer(rg.get_buf(src_buf), src_offset, rg.get_external_buffer(dst_rg_id),
                               dst_offset, size_bytes);
  });
}

// Mirrors `encode_meshlet_mesh_draw_pass` in DrawPassSceneBindings.cpp; uses explicit task flags
// (no renderer_cv) so vktest stays self-contained.
void encode_meshlet_test_draw_pass(
    bool reverse_z, bool late_pass, uint32_t meshlet_task_flags, rhi::Device* device,
    RenderGraph& rg, const GeometryBatch& batch, rhi::BufferHandle materials_buf,
    const BufferSuballoc& globals_cb, const BufferSuballoc& view_cb, const BufferSuballoc& cull_cb,
    rhi::TextureHandle depth_pyramid_tex, glm::ivec2 viewport_dims, RGResourceId meshlet_vis_rg,
    RGResourceId meshlet_stats_rg, RGResourceId task_cmd_rg, rhi::BufferHandle indirect_buf,
    InstanceMgr& inst_mgr, std::span<const rhi::PipelineHandleHolder> psos, rhi::CmdEncoder* enc) {
  ASSERT(psos.size() == static_cast<size_t>(AlphaMaskType::Count));
  enc->set_wind_order(rhi::WindOrder::Clockwise);
  enc->set_cull_mode(rhi::CullMode::None);
  enc->set_viewport({0, 0}, viewport_dims);
  enc->set_scissor({0, 0}, viewport_dims);

  enc->bind_uav(rg.get_external_buffer(meshlet_vis_rg), 1);
  if (late_pass) {
    enc->bind_srv(depth_pyramid_tex, 3);
  }

  enc->bind_uav(rg.get_buf(meshlet_stats_rg), 2);
  enc->bind_srv(batch.mesh_buf.get_buffer_handle(), 5);
  enc->bind_srv(batch.meshlet_buf.get_buffer_handle(), 6);
  enc->bind_srv(batch.meshlet_triangles_buf.get_buffer_handle(), 7);
  enc->bind_srv(batch.meshlet_vertices_buf.get_buffer_handle(), 8);
  enc->bind_srv(batch.vertex_buf.get_buffer_handle(), 9);
  enc->bind_srv(inst_mgr.get_instance_data_buf(), 10);
  enc->bind_srv(materials_buf, 11);
  enc->bind_cbv(globals_cb.buf, GLOBALS_SLOT, globals_cb.offset_bytes, sizeof(GlobalData));
  enc->bind_cbv(view_cb.buf, VIEW_DATA_SLOT, view_cb.offset_bytes, sizeof(ViewData));
  enc->bind_cbv(cull_cb.buf, 4, cull_cb.offset_bytes, sizeof(CullData));

  Task2PC pc{.flags = meshlet_task_flags,
             .out_draw_count_buf_idx = device->get_buf(indirect_buf)->bindless_idx()};

  TaskCmdBufRgIdsByAlphaMask task_ids{};
  task_ids[AlphaMaskType::Opaque] = task_cmd_rg;
  task_ids[AlphaMaskType::Mask] = task_cmd_rg;

  for (size_t alpha_mask_type = 0; alpha_mask_type < static_cast<size_t>(AlphaMaskType::Count);
       alpha_mask_type++) {
    enc->bind_srv(rg.get_buf(task_ids[static_cast<AlphaMaskType>(alpha_mask_type)]), 4);
    enc->bind_pipeline(psos[alpha_mask_type]);
    enc->set_depth_stencil_state(reverse_z ? rhi::CompareOp::Greater : rhi::CompareOp::Less, true);
    pc.alpha_test_enabled = static_cast<uint32_t>(alpha_mask_type);
    enc->push_constants(&pc, sizeof(pc));
    enc->draw_mesh_threadgroups_indirect(
        indirect_buf, static_cast<uint32_t>(alpha_mask_type) * sizeof(uint32_t) * 3,
        {K_TASK_TG_SIZE, 1, 1}, {K_MESH_TG_SIZE, 1, 1});
  }
}

glm::vec3 safe_normalize_toward_light(const glm::vec3& v) {
  const float s2 = glm::dot(v, v);
  if (s2 < 1e-12f) {
    return glm::normalize(glm::vec3(0.35f, 1.f, 0.4f));
  }
  return v * (1.f / std::sqrt(s2));
}

}  // namespace

void MeshletRendererScene::update_toward_light_effective(const TestSceneContext& ctx) {
  if (day_night_cycle_ && !day_night_cycle_paused_) {
    const float period = std::max(1e-4f, day_cycle_period_sec_);
    day_cycle_time_sec_ = std::fmodf(day_cycle_time_sec_ + ctx.delta_time_sec, period);
    if (day_cycle_time_sec_ < 0.f) {
      day_cycle_time_sec_ += period;
    }
  }

  if (day_night_cycle_) {
    const float period = std::max(1e-4f, day_cycle_period_sec_);
    const float phase_wrapped = std::fmodf(day_cycle_time_sec_, period);
    const float t = 2.0f * std::numbers::pi_v<float> * (phase_wrapped / period) -
                    0.5f * std::numbers::pi_v<float>;  // 2pi * f - pi/2
    const glm::vec3 raw(0.35f * std::cos(t), std::sin(t), 0.4f * std::cos(t * 0.5f));
    toward_light_effective_ = safe_normalize_toward_light(raw);
  } else {
    toward_light_effective_ = safe_normalize_toward_light(toward_light_manual_);
  }
}

MeshletRendererScene::MeshletRendererScene(const TestSceneContext& ctx)
    : ITestScene(ctx), frame_uniform_gpu_allocator_(ctx_.device, true) {
  ASSERT(ctx_.model_gpu_mgr != nullptr);
  ASSERT(ctx_.shader_mgr != nullptr);
  ASSERT(ctx_.frame_staging != nullptr);

  generate_task_cmd_compute_pass_.emplace(*ctx_.device, *ctx_.rg, *ctx_.model_gpu_mgr,
                                          *ctx_.shader_mgr);

  for (size_t a = 0; a < static_cast<size_t>(AlphaMaskType::Count); ++a) {
    meshlet_pso_early_[a] = ctx_.shader_mgr->create_graphics_pipeline({
        .shaders = {{{"forward_meshlet", ShaderType::Task},
                     {"debug_meshlet_hello", ShaderType::Mesh},
                     {"debug_meshlet_hello", ShaderType::Fragment}}},
        .name = std::string("meshlet_test_forward_early_") + std::to_string(a),
    });
    meshlet_pso_late_[a] = ctx_.shader_mgr->create_graphics_pipeline({
        .shaders = {{{"forward_meshlet_late", ShaderType::Task},
                     {"debug_meshlet_hello", ShaderType::Mesh},
                     {"debug_meshlet_hello", ShaderType::Fragment}}},
        .name = std::string("meshlet_test_forward_late_") + std::to_string(a),
    });
    meshlet_pso_shadow_[a] = ctx_.shader_mgr->create_graphics_pipeline({
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
  shade_pso_ = ctx_.shader_mgr->create_graphics_pipeline({
      .shaders = {{{"fullscreen_quad", ShaderType::Vertex},
                   {"meshlet_test/shade", ShaderType::Fragment}}},
      .name = "meshlet_test/shade",
  });
  clear_mesh_indirect_pso_ = ctx_.shader_mgr->create_compute_pipeline(
      {.path = "test_clear_cnt_buf", .type = rhi::ShaderType::Compute});
  depth_reduce_pso_ = ctx_.shader_mgr->create_compute_pipeline(
      {.path = "depth_reduce/depth_reduce", .type = rhi::ShaderType::Compute});

  for (int i = 0; i < k_max_frames_in_flight; i++) {
    task_cmd_group_count_readback_[static_cast<size_t>(i)] = ctx_.device->create_buf_h({
        .size = sizeof(uint32_t),
        .flags = rhi::BufferDescFlags::CPUAccessible,
        .name = "meshlet_draw_count_readback",
    });
    visible_object_count_readback_[static_cast<size_t>(i)] = ctx_.device->create_buf_h({
        .size = sizeof(uint32_t),
        .flags = rhi::BufferDescFlags::CPUAccessible,
        .name = "meshlet_visible_object_count_readback",
    });
    meshlet_stats_buf_readback_[static_cast<size_t>(i)] = ctx_.device->create_buf_h({
        .size = k_meshlet_draw_stats_bytes,
        .flags = rhi::BufferDescFlags::CPUAccessible,
        .name = "meshlet_draw_stats_readback",
    });
  }

  load_scene_presets();

  make_depth_pyramid_tex();
  toward_light_effective_ = safe_normalize_toward_light(toward_light_manual_);
}

void MeshletRendererScene::load_scene_presets() {
  scene_presets_.clear();
  ScenePresetLoaders loaders{
      .add_model =
          [this](const std::filesystem::path& p, const glm::mat4& t) {
            models_.emplace_back(ResourceManager::get().load_model(p, t));
          },
      .add_instanced =
          [this](const std::filesystem::path& p, std::vector<glm::mat4>&& tf) {
            ResourceManager::InstancedModelLoadRequest req{.path = p,
                                                           .instance_transforms = std::move(tf)};
            auto result = ResourceManager::get().load_instanced_models(std::span(&req, 1));
            for (auto& r : result) {
              models_.reserve(models_.size() + r.size());
              models_.insert(models_.end(), std::make_move_iterator(r.begin()),
                             std::make_move_iterator(r.end()));
            }
          },
  };
  append_default_scene_presets(scene_presets_, ctx_.resource_dir, loaders);
}

void MeshletRendererScene::clear_all_models() {
  for (auto& m : models_) {
    ResourceManager::get().free_model(m);
  }
  models_.clear();
}

void MeshletRendererScene::apply_preset(size_t idx) {
  if (scene_presets_.empty() || idx >= scene_presets_.size()) {
    return;
  }
  auto& preset = scene_presets_[idx];
  fps_camera_.camera() = preset.cam;
  fps_camera_.camera().calc_vectors();
  clear_all_models();
  preset.load_fn();
}

void MeshletRendererScene::apply_demo_scene_preset(size_t index) {
  if (scene_presets_.empty()) {
    return;
  }
  const size_t idx = std::min(index, scene_presets_.size() - 1);
  apply_preset(idx);
}

void MeshletRendererScene::on_swapchain_resize() { make_depth_pyramid_tex(); }

void MeshletRendererScene::make_depth_pyramid_tex() {
  if (!ctx_.swapchain || ctx_.swapchain->desc_.width == 0 || ctx_.swapchain->desc_.height == 0) {
    return;
  }
  const glm::uvec2 main_size{ctx_.swapchain->desc_.width, ctx_.swapchain->desc_.height};
  const glm::uvec3 size{prev_pow2(main_size.x), prev_pow2(main_size.y), 1u};

  if (depth_pyramid_tex_.is_valid()) {
    if (auto* existing = ctx_.device->get_tex(depth_pyramid_tex_);
        existing && existing->desc().dims == size) {
      return;
    }
  }

  for (auto v : depth_pyramid_tex_.views) {
    ctx_.device->destroy(depth_pyramid_tex_.handle, v);
  }
  depth_pyramid_tex_.views.clear();
  const auto mip_levels = static_cast<uint32_t>(math::get_mip_levels(size.x, size.y));
  depth_pyramid_tex_ = rhi::TexAndViewHolder{ctx_.device->create_tex_h(rhi::TextureDesc{
      .format = rhi::TextureFormat::R32float,
      .usage =
          rhi::TextureUsage::Storage | rhi::TextureUsage::ShaderWrite | rhi::TextureUsage::Sample,
      .dims = size,
      .mip_levels = mip_levels,
      .name = "meshlet_test_depth_pyramid",
  })};
  depth_pyramid_tex_.views.reserve(mip_levels);
  for (uint32_t i = 0; i < mip_levels; i++) {
    depth_pyramid_tex_.views.push_back(
        ctx_.device->create_tex_view(depth_pyramid_tex_.handle, i, 1, 0, 1));
  }
  debug_depth_pyramid_mip_ =
      std::clamp(debug_depth_pyramid_mip_, 0, std::max(0, static_cast<int>(mip_levels) - 1));
}

void MeshletRendererScene::shutdown() {
  if (ctx_.window) {
    fps_camera_.set_mouse_captured(ctx_.window->get_handle(), false);
  }
  if (cached_shadow_depth_tex_.is_valid()) {
    for (auto& view : shadow_depth_layer_views_) {
      if (view >= 0) {
        ctx_.device->destroy(cached_shadow_depth_tex_, view);
        view = -1;
      }
    }
    cached_shadow_depth_tex_ = {};
  }
  clear_all_models();
}

void MeshletRendererScene::on_frame(const TestSceneContext& ctx) {
  update_toward_light_effective(ctx);
  const bool imgui_blocks =
      ctx.imgui_ui_active && (ImGui::GetIO().WantTextInput || ImGui::GetIO().WantCaptureKeyboard);
  if (ctx.window) {
    fps_camera_.update(ctx.window->get_handle(), ctx.delta_time_sec, imgui_blocks);
  }
}

void MeshletRendererScene::on_cursor_pos(double x, double y) { fps_camera_.on_cursor_pos(x, y); }

void MeshletRendererScene::on_key_event(int key, int action, int mods) {
  (void)mods;
  if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE && ctx_.window) {
    fps_camera_.toggle_mouse_capture(ctx_.window->get_handle());
  }
}

void MeshletRendererScene::on_imgui() {
  ImGui::Begin("Meshlet renderer");
  if (!scene_presets_.empty()) {
    ImGui::SeparatorText("Scene presets");
    static int scene_preset_selection = 0;
    scene_preset_selection =
        std::clamp(scene_preset_selection, 0, static_cast<int>(scene_presets_.size()) - 1);
    const float list_h = ImGui::GetTextLineHeightWithSpacing() * 7.0f;
    if (ImGui::BeginListBox("##scene_presets", ImVec2(-FLT_MIN, list_h))) {
      for (int i = 0; i < static_cast<int>(scene_presets_.size()); ++i) {
        ImGui::PushID(i);
        const bool selected = (scene_preset_selection == i);
        if (ImGui::Selectable(scene_presets_[static_cast<size_t>(i)].name.c_str(), selected,
                              ImGuiSelectableFlags_AllowDoubleClick)) {
          scene_preset_selection = i;
          if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            apply_preset(static_cast<size_t>(i));
          }
        }
        ImGui::PopID();
      }
      ImGui::EndListBox();
    }
    if (ImGui::Button("Load preset", ImVec2(-FLT_MIN, 0))) {
      apply_preset(static_cast<size_t>(scene_preset_selection));
    }
    ImGui::Separator();
  }
  ImGui::SeparatorText("Lighting");
  {
    const bool was_day_night = day_night_cycle_;
    ImGui::BeginDisabled(day_night_cycle_);
    ImGui::DragFloat3("Toward light (world)", &toward_light_manual_.x, 0.01f);
    ImGui::EndDisabled();
    ImGui::Checkbox("Day / night cycle", &day_night_cycle_);
    if (was_day_night && !day_night_cycle_) {
      toward_light_manual_ = toward_light_effective_;
    }
    ImGui::BeginDisabled(!day_night_cycle_);
    ImGui::Checkbox("Pause cycle", &day_night_cycle_paused_);
    ImGui::SliderFloat("Day length (s)", &day_cycle_period_sec_, 10.0f, 600.0f);
    ImGui::EndDisabled();
    ImGui::Text("Effective (normalized): %.3f, %.3f, %.3f", toward_light_effective_.x,
                toward_light_effective_.y, toward_light_effective_.z);
  }
  ImGui::Checkbox("GPU object frustum cull", &gpu_object_frustum_cull_);
  ImGui::Checkbox("GPU object occlusion cull", &gpu_object_occlusion_cull_);
  int shadow_cascade_count = static_cast<int>(shadow_cfg_.cascade_count);
  if (ImGui::SliderInt("Shadow cascades", &shadow_cascade_count, 1,
                       static_cast<int>(shadow_cfg_.max_cascades))) {
    shadow_cfg_.cascade_count = static_cast<uint32_t>(
        std::clamp(shadow_cascade_count, 1, static_cast<int>(shadow_cfg_.max_cascades)));
  }
  ImGui::SliderFloat("Shadow split lambda", &shadow_cfg_.split_lambda, 0.0f, 1.0f);
  ImGui::SliderFloat("Shadow bias min", &shadow_cfg_.bias_min, 0.0f, 0.01f, "%.5f");
  ImGui::SliderFloat("Shadow bias max", &shadow_cfg_.bias_max, 0.0f, 0.02f, "%.5f");
  ImGui::Checkbox("Visualize shadow cascades", &visualize_shadow_cascades_);
  ImGui::SeparatorText("CSM debug");
  {
    const int last_cascade = static_cast<int>(shadow_cfg_.cascade_count) - 1;
    if (last_cascade >= 0) {
      debug_csm_cascade_layer_ = std::clamp(debug_csm_cascade_layer_, 0, last_cascade);
    }
    ImGui::SliderInt("View cascade (depth)", &debug_csm_cascade_layer_, 0,
                     std::max(0, last_cascade));
    if (cached_shadow_depth_tex_.is_valid() && shadow_cfg_.cascade_count > 0) {
      const uint32_t array_b = ctx_.device->get_tex(cached_shadow_depth_tex_)->bindless_idx();
      const uint32_t layer = static_cast<uint32_t>(std::max(0, debug_csm_cascade_layer_));
      const float disp_w = 240.f;
      const float disp_h = disp_w;  // square shadow map
      ImGui::Image(MakeImGuiTexRefCsmArraySlice(array_b, layer), ImVec2(disp_w, disp_h),
                   ImVec2(0, 0), ImVec2(1, 1));
    } else {
      ImGui::TextUnformatted("CSM depth preview (not ready or no CSM this frame)");
    }
  }
  uint32_t visible_meshlet_task_groups = 0;
  uint32_t visible_objects = 0;
  MeshletDrawStats visible_meshlet_stats{};
  if (frame_num_ >= ctx_.device->get_info().frames_in_flight) {
    const uint32_t curr = ctx_.curr_frame_in_flight_idx;
    visible_meshlet_task_groups = *reinterpret_cast<const uint32_t*>(
        ctx_.device->get_buf(task_cmd_group_count_readback_[curr].handle)->contents());
    visible_objects = *reinterpret_cast<const uint32_t*>(
        ctx_.device->get_buf(visible_object_count_readback_[curr].handle)->contents());
    visible_meshlet_stats = *reinterpret_cast<const MeshletDrawStats*>(
        ctx_.device->get_buf(meshlet_stats_buf_readback_[curr].handle)->contents());
  }
  ImGui::Text("Visible mesh task groups (GPU): %u", visible_meshlet_task_groups);
  ImGui::Text("Visible objects (GPU): %u", visible_objects);
  ImGui::Text("Visible meshlets (GPU): %u", visible_meshlet_stats.meshlets_drawn_early +
                                                visible_meshlet_stats.meshlets_drawn_late);
  ImGui::Text("Visible triangles (GPU): %u", visible_meshlet_stats.triangles_drawn_early +
                                                 visible_meshlet_stats.triangles_drawn_late);

  if (depth_pyramid_tex_.is_valid()) {
    auto* dp_tex = ctx_.device->get_tex(depth_pyramid_tex_);
    const glm::uvec2 dp_dims{dp_tex->desc().dims};
    const int mip_levels = static_cast<int>(
        math::get_mip_levels(static_cast<size_t>(dp_dims.x), static_cast<size_t>(dp_dims.y)));
    ImGui::Separator();
    if (mip_levels > 1) {
      ImGui::SliderInt("Depth pyramid mip", &debug_depth_pyramid_mip_, 0,
                       std::max(0, mip_levels - 1));
      const int mip = std::clamp(debug_depth_pyramid_mip_, 0, std::max(0, mip_levels - 1));
      const auto mip_u = static_cast<uint32_t>(mip);
      const uint32_t mv_w = std::max(1u, dp_dims.x >> mip_u);
      const uint32_t mv_h = std::max(1u, dp_dims.y >> mip_u);
      const uint32_t view_bindless = ctx_.device->get_tex_view_bindless_idx(
          depth_pyramid_tex_.handle, depth_pyramid_tex_.views[static_cast<size_t>(mip)]);
      const float disp_w = 240.f;
      const float disp_h = disp_w * static_cast<float>(mv_h) / static_cast<float>(mv_w);
      ImGui::Image(MakeImGuiTexRefBindlessFloatView(view_bindless), ImVec2(disp_w, disp_h),
                   ImVec2(0, 0), ImVec2(1, 1));
    } else {
      ImGui::TextUnformatted("Depth pyramid (single mip; reduce skipped)");
    }
  }

  ImGui::End();
}

ViewData MeshletRendererScene::prepare_view_data() {
  const float aspect = static_cast<float>(ctx_.swapchain->desc_.width) /
                       std::max(1.f, static_cast<float>(ctx_.swapchain->desc_.height));
  float fov = 60.f;
  const glm::mat4 proj = infinite_perspective_proj(glm::radians(fov), aspect, 0.1f);
  fps_camera_.camera().calc_vectors();
  const glm::mat4 view = fps_camera_.camera().get_view_mat();
  const glm::mat4 vp = proj * view;

  ViewData vd{};
  vd.vp = vp;
  vd.inv_vp = glm::inverse(vp);
  vd.view = view;
  vd.proj = proj;
  vd.inv_proj = glm::inverse(proj);
  vd.camera_pos = glm::vec4(fps_camera_.camera().pos, 1.f);
  return vd;
}

CullData MeshletRendererScene::prepare_cull_data_for_proj(const glm::mat4& proj, float z_near,
                                                          float z_far) const {
  const glm::mat4 projection_transpose = glm::transpose(proj);
  auto normalize_plane = [](const glm::vec4& plane) {
    const glm::vec3 n = glm::vec3(plane);
    const float inv_len = 1.f / glm::length(n);
    return glm::vec4(n * inv_len, plane.w * inv_len);
  };
  const glm::vec4 frustum_x = normalize_plane(projection_transpose[0] + projection_transpose[3]);
  const glm::vec4 frustum_y = normalize_plane(projection_transpose[1] + projection_transpose[3]);

  CullData cd{};
  cd.frustum = glm::vec4(frustum_x.x, frustum_x.z, frustum_y.y, frustum_y.z);
  cd.z_near = z_near;
  cd.z_far = z_far;
  cd.p00 = proj[0][0];
  cd.p11 = proj[1][1];
  cd.pyramid_width = 0;
  cd.pyramid_height = 0;
  cd.pyramid_mip_count = 0;
  cd.paused = 0;
  return cd;
}

CullData MeshletRendererScene::prepare_cull_data(const ViewData& vd) const {
  return prepare_cull_data_for_proj(vd.proj, 0.1f, 10'000.f);
}

CullData MeshletRendererScene::prepare_cull_data_late(const ViewData& vd) const {
  CullData cd = prepare_cull_data(vd);
  if (depth_pyramid_tex_.is_valid()) {
    const auto* dp = ctx_.device->get_tex(depth_pyramid_tex_);
    cd.pyramid_width = dp->desc().dims.x;
    cd.pyramid_height = dp->desc().dims.y;
    cd.pyramid_mip_count = dp->desc().mip_levels;
  }
  return cd;
}

MeshletRendererScene::ShadowFrameData MeshletRendererScene::build_shadow_frame_data(
    const ViewData& camera_view, const glm::vec3& toward_light) const {
  ShadowFrameData out{};
  const uint32_t max_cascades =
      std::min(shadow_cfg_.max_cascades, static_cast<uint32_t>(CSM_MAX_CASCADES));
  const uint32_t cascade_count = std::clamp(shadow_cfg_.cascade_count, 1u, max_cascades);
  out.cascade_count = cascade_count;
  out.csm_data.num_cascades = cascade_count;
  out.csm_data.biases =
      glm::vec4(shadow_cfg_.bias_min, shadow_cfg_.bias_max, 0.0f, shadow_cfg_.z_far);
  out.csm_data.cascade_levels = glm::vec4(shadow_cfg_.z_far);

  // `toward_light` points from surface -> light. Shadow camera must look along
  // light rays (light -> surface), which `lookAt(center + dir, center)` achieves
  // when `dir` is surface -> light.
  const glm::vec3 light_dir_ws = glm::normalize(toward_light);
  const float aspect = static_cast<float>(ctx_.swapchain->desc_.width) /
                       std::max(1.f, static_cast<float>(ctx_.swapchain->desc_.height));
  constexpr float k_fov_deg = 60.f;

  for (uint32_t i = 0; i + 1 < cascade_count; i++) {
    const float p = (i + 1) / static_cast<float>(cascade_count);
    const float log_split =
        shadow_cfg_.z_near * std::pow(shadow_cfg_.z_far / shadow_cfg_.z_near, p);
    const float linear_split = shadow_cfg_.z_near + ((shadow_cfg_.z_far - shadow_cfg_.z_near) * p);
    out.csm_data.cascade_levels[i] =
        (shadow_cfg_.split_lambda * log_split) + ((1.0f - shadow_cfg_.split_lambda) * linear_split);
  }

  auto get_proj = [&](float near_z, float far_z) {
    return glm::perspectiveRH_ZO(glm::radians(k_fov_deg), aspect, near_z, far_z);
  };

  for (uint32_t cascade_i = 0; cascade_i < cascade_count; cascade_i++) {
    const float split_near =
        (cascade_i == 0) ? shadow_cfg_.z_near : out.csm_data.cascade_levels[cascade_i - 1];
    const float split_far = (cascade_i + 1 == cascade_count)
                                ? shadow_cfg_.z_far
                                : out.csm_data.cascade_levels[cascade_i];
    glm::mat4 light_proj{};
    glm::mat4 light_view{};
    const glm::mat4 light_vp = calc_light_space_vp(
        camera_view.view, get_proj(split_near, split_far), light_dir_ws,
        static_cast<float>(shadow_cfg_.shadow_map_resolution), light_proj, light_view);
    out.csm_data.light_vp_matrices[cascade_i] = light_vp;
    ViewData& cascade_vd = out.view_data[cascade_i];
    cascade_vd.vp = light_vp;
    cascade_vd.inv_vp = glm::inverse(light_vp);
    cascade_vd.view = light_view;
    cascade_vd.proj = light_proj;
    cascade_vd.inv_proj = glm::inverse(light_proj);
    cascade_vd.camera_pos = glm::vec4(0.f, 0.f, 0.f, 1.f);
    out.cull_data[cascade_i] = prepare_cull_data_for_proj(light_proj, split_near, split_far);
  }

  return out;
}

void MeshletRendererScene::add_render_graph_passes() {
  auto& batch = ctx_.model_gpu_mgr->geometry_batch();
  auto task_cmd_count = batch.task_cmd_count;
  // TODO: still need to render the ui, etc.
  if (task_cmd_count == 0 || batch.get_stats().vertex_count == 0) {
    return;
  }

  if (ctx_.swapchain) {
    make_depth_pyramid_tex();
  }
  ASSERT(depth_pyramid_tex_.is_valid());

  bool meshlet_vis_buffer_reallocated = false;
  {
    const size_t n = ctx_.model_gpu_mgr->instance_mgr().get_num_meshlet_vis_buf_elements();
    const size_t need = n * sizeof(uint32_t);
    if (need == 0) {
      return;
    }
    rhi::Buffer* cur =
        meshlet_vis_buf_.handle.is_valid() ? ctx_.device->get_buf(meshlet_vis_buf_) : nullptr;
    if (cur == nullptr || cur->size() < need) {
      if (meshlet_vis_buf_.handle.is_valid()) {
        ctx_.device->destroy(meshlet_vis_buf_.handle);
        meshlet_vis_buf_ = {};
      }
      meshlet_vis_buf_ = ctx_.device->create_buf_h({
          .usage = rhi::BufferUsage::Storage,
          .size = need,
          .name = "meshlet_test_meshlet_vis",
      });
      meshlet_vis_buffer_reallocated = true;
    }
  }
  if (!meshlet_vis_buf_.handle.is_valid()) {
    return;
  }

  const bool meshlet_vis_cleared_this_frame = (frame_num_ == 0 || meshlet_vis_buffer_reallocated);
  const RGState meshlet_vis_import_initial =
      meshlet_vis_cleared_this_frame
          ? RGState{}
          : RGState{.access = AccessFlags::ShaderRead | AccessFlags::ShaderWrite,
                    .stage = PipelineStage::TaskShader};
  RGResourceId meshlet_vis_rg_id = ctx_.rg->import_external_buffer(
      meshlet_vis_buf_.handle, meshlet_vis_import_initial, "meshlet_test_meshlet_vis_rg");

  if (meshlet_vis_cleared_this_frame) {
    const size_t need =
        ctx_.model_gpu_mgr->instance_mgr().get_num_meshlet_vis_buf_elements() * sizeof(uint32_t);
    auto& p = ctx_.rg->add_transfer_pass("meshlet_clear_meshlet_vis");
    meshlet_vis_rg_id = p.write_buf(meshlet_vis_rg_id, rhi::PipelineStage::AllTransfer);
    p.set_ex([need, this](rhi::CmdEncoder* enc) {
      enc->fill_buffer(meshlet_vis_buf_.handle, 0, static_cast<uint32_t>(need), 1);
    });
  }

  frame_uniform_gpu_allocator_.set_frame_idx(ctx_.curr_frame_in_flight_idx);

  frame_num_++;

  auto vd = prepare_view_data();
  auto view_cb_suballoc = frame_uniform_gpu_allocator_.alloc2(sizeof(ViewData), &vd);

  auto cd_early = prepare_cull_data(vd);
  auto cull_early_cb = frame_uniform_gpu_allocator_.alloc2(sizeof(CullData), &cd_early);
  auto cd_late = prepare_cull_data_late(vd);
  auto cull_late_cb = frame_uniform_gpu_allocator_.alloc2(sizeof(CullData), &cd_late);

  BufferSuballoc globals_cb_buf;
  BufferSuballoc shadow_globals_cb_buf;
  const glm::vec3& toward_light = toward_light_effective_;
  {
    GlobalData gd{};
    gd.render_mode =
        visualize_shadow_cascades_ ? DEBUG_RENDER_MODE_CSM_CASCADE_COLORS : DEBUG_RENDER_MODE_NONE;
    gd.frame_num = frame_num_;
    gd.meshlet_stats_enabled = 1;
    gd._padding = 0;
    gd.diffuse_light_dir_world = glm::vec4(toward_light, 0.f);
    globals_cb_buf = frame_uniform_gpu_allocator_.alloc2(sizeof(GlobalData), &gd);

    GlobalData shadow_gd = gd;
    shadow_gd.meshlet_stats_enabled = 0;
    shadow_gd.render_mode = DEBUG_RENDER_MODE_NONE;
    shadow_globals_cb_buf = frame_uniform_gpu_allocator_.alloc2(sizeof(GlobalData), &shadow_gd);
  }

  ShadowFrameData shadow_frame_data = build_shadow_frame_data(vd, toward_light);
  BufferSuballoc shadow_csm_cb =
      frame_uniform_gpu_allocator_.alloc2(sizeof(CSMData), &shadow_frame_data.csm_data);
  std::array<BufferSuballoc, CSM_MAX_CASCADES> shadow_view_cbs{};
  std::array<BufferSuballoc, CSM_MAX_CASCADES> shadow_cull_cbs{};
  for (uint32_t cascade_i = 0; cascade_i < shadow_frame_data.cascade_count; cascade_i++) {
    shadow_view_cbs[cascade_i] = frame_uniform_gpu_allocator_.alloc2(
        sizeof(ViewData), &shadow_frame_data.view_data[cascade_i]);
    shadow_cull_cbs[cascade_i] = frame_uniform_gpu_allocator_.alloc2(
        sizeof(CullData), &shadow_frame_data.cull_data[cascade_i]);
  }

  constexpr size_t k_indirect_bytes =
      sizeof(uint32_t) * 3 * static_cast<size_t>(AlphaMaskType::Count);

  RGResourceId instance_vis_current_rg{};
  const uint32_t max_draws = ctx_.model_gpu_mgr->instance_mgr().stats().max_instance_data_count;
  if (gpu_object_occlusion_cull_) {
    const size_t required = static_cast<size_t>(max_draws) * sizeof(uint32_t);
    instance_vis_current_rg = ctx_.rg->create_buffer(
        {.size = required, .temporal = true, .temporal_slot_mode = TemporalSlotMode::SingleSlot},
        "meshlet_test_instance_vis");
    if (!ctx_.rg->has_history(instance_vis_current_rg)) {
      auto& p = ctx_.rg->add_transfer_pass("meshlet_prime_instance_vis");
      instance_vis_current_rg =
          p.write_buf(instance_vis_current_rg, rhi::PipelineStage::AllTransfer);
      const RGResourceId instance_vis_rg = instance_vis_current_rg;
      p.set_ex([this, required, instance_vis_rg](CmdEncoder* enc) {
        enc->fill_buffer(ctx_.rg->get_buf(instance_vis_rg), 0, static_cast<uint32_t>(required), 1);
      });
    }
  }

  RGResourceId task_cmd_early_rg = ctx_.rg->create_buffer(
      {.size = task_cmd_count * sizeof(TaskCmd)}, "meshlet_hello_task_cmds_early");
  RGResourceId task_cmd_late_rg = ctx_.rg->create_buffer({.size = task_cmd_count * sizeof(TaskCmd)},
                                                         "meshlet_hello_task_cmds_late");

  RGResourceId indirect_args_early_rg =
      ctx_.rg->create_buffer({.size = k_indirect_bytes}, "meshlet_hello_indirect_args_early");
  RGResourceId indirect_args_late_rg =
      ctx_.rg->create_buffer({.size = k_indirect_bytes}, "meshlet_hello_indirect_args_late");
  std::array<RGResourceId, CSM_MAX_CASCADES> task_cmd_shadow_rg{};
  std::array<RGResourceId, CSM_MAX_CASCADES> indirect_args_shadow_rg{};
  for (uint32_t cascade_i = 0; cascade_i < shadow_frame_data.cascade_count; cascade_i++) {
    task_cmd_shadow_rg[cascade_i] =
        ctx_.rg->create_buffer({.size = task_cmd_count * sizeof(TaskCmd)},
                               "meshlet_shadow_task_cmds_" + std::to_string(cascade_i));
    indirect_args_shadow_rg[cascade_i] = ctx_.rg->create_buffer(
        {.size = k_indirect_bytes}, "meshlet_shadow_indirect_args_" + std::to_string(cascade_i));
  }

  RGResourceId visible_object_count_rg = ctx_.rg->create_buffer(
      {.size = sizeof(uint32_t), .defer_reuse = true}, "meshlet_visible_object_count");
  RGResourceId meshlet_stats_rg = ctx_.rg->create_buffer(
      {.size = k_meshlet_draw_stats_bytes, .defer_reuse = true}, "meshlet_test_draw_stats");

  {
    auto& p = ctx_.rg->add_transfer_pass("meshlet_clear_visible_count_buf_and_stats");
    visible_object_count_rg = p.write_buf(visible_object_count_rg, rhi::PipelineStage::AllTransfer);
    meshlet_stats_rg = p.write_buf(meshlet_stats_rg, rhi::PipelineStage::AllTransfer);
    p.set_ex([this, visible_object_count_rg, meshlet_stats_rg](CmdEncoder* enc) {
      enc->fill_buffer(ctx_.rg->get_buf(visible_object_count_rg), 0, sizeof(uint32_t), 0);
      enc->fill_buffer(ctx_.rg->get_buf(meshlet_stats_rg), 0, k_meshlet_draw_stats_bytes, 0);
    });
  }

  {
    auto& p = ctx_.rg->add_compute_pass("meshlet_clear_indirect_mesh_cmds");
    indirect_args_early_rg = p.write_buf(indirect_args_early_rg, rhi::PipelineStage::ComputeShader);
    indirect_args_late_rg = p.write_buf(indirect_args_late_rg, rhi::PipelineStage::ComputeShader);
    p.set_ex([this, indirect_args_early_rg, indirect_args_late_rg](CmdEncoder* enc) {
      enc->bind_pipeline(clear_mesh_indirect_pso_);
      for (RGResourceId id : {indirect_args_early_rg, indirect_args_late_rg}) {
        TestClearBufPC pc{
            .buf_idx = ctx_.device->get_buf(ctx_.rg->get_buf(id))->bindless_idx(),
        };
        enc->push_constants(&pc, sizeof(pc));
        enc->dispatch_compute({static_cast<uint32_t>(AlphaMaskType::Count), 1u, 1u}, {1u, 1u, 1u});
      }
    });
  }

  if (shadow_frame_data.cascade_count > 0) {
    auto& p = ctx_.rg->add_compute_pass("meshlet_clear_shadow_indirect_mesh_cmds");
    for (uint32_t cascade_i = 0; cascade_i < shadow_frame_data.cascade_count; cascade_i++) {
      indirect_args_shadow_rg[cascade_i] =
          p.write_buf(indirect_args_shadow_rg[cascade_i], rhi::PipelineStage::ComputeShader);
    }
    const auto shadow_indirect = indirect_args_shadow_rg;
    p.set_ex([this, shadow_indirect,
              cascade_count = shadow_frame_data.cascade_count](CmdEncoder* enc) {
      enc->bind_pipeline(clear_mesh_indirect_pso_);
      for (uint32_t cascade_i = 0; cascade_i < cascade_count; cascade_i++) {
        TestClearBufPC pc{
            .buf_idx =
                ctx_.device->get_buf(ctx_.rg->get_buf(shadow_indirect[cascade_i]))->bindless_idx(),
        };
        enc->push_constants(&pc, sizeof(pc));
        enc->dispatch_compute({static_cast<uint32_t>(AlphaMaskType::Count), 1u, 1u}, {1u, 1u, 1u});
      }
    });
  }

  generate_task_cmd_compute_pass_->bake(
      max_draws, false, gpu_object_frustum_cull_, gpu_object_occlusion_cull_, view_cb_suballoc,
      cull_early_cb, task_cmd_early_rg, indirect_args_early_rg, visible_object_count_rg,
      &instance_vis_current_rg, nullptr, rhi::TextureHandle{});

  RGResourceId shadow_visible_object_count_rg = ctx_.rg->create_buffer(
      {.size = sizeof(uint32_t), .defer_reuse = true}, "meshlet_shadow_visible_object_count");
  {
    auto& p = ctx_.rg->add_transfer_pass("meshlet_clear_shadow_visible_count");
    shadow_visible_object_count_rg =
        p.write_buf(shadow_visible_object_count_rg, rhi::PipelineStage::AllTransfer);
    p.set_ex([this, shadow_visible_object_count_rg](CmdEncoder* enc) {
      enc->fill_buffer(ctx_.rg->get_buf(shadow_visible_object_count_rg), 0, sizeof(uint32_t), 0);
    });
  }
  for (uint32_t cascade_i = 0; cascade_i < shadow_frame_data.cascade_count; cascade_i++) {
    generate_task_cmd_compute_pass_->bake(
        max_draws, false, false, false, shadow_view_cbs[cascade_i], shadow_cull_cbs[cascade_i],
        task_cmd_shadow_rg[cascade_i], indirect_args_shadow_rg[cascade_i],
        shadow_visible_object_count_rg, nullptr, nullptr, rhi::TextureHandle{});
  }

  RGResourceId gbuffer_a_id =
      ctx_.rg->create_texture({.format = rhi::TextureFormat::R16G16B16A16Sfloat}, "gbuffer_a");
  RGResourceId gbuffer_b_id =
      ctx_.rg->create_texture({.format = rhi::TextureFormat::R16G16B16A16Sfloat}, "gbuffer_b");
  RGResourceId depth_att = ctx_.rg->create_texture(
      {.format = TextureFormat::D32float, .size_class = SizeClass::Swapchain},
      "meshlet_hello_depth_att");
  RGResourceId shadow_depth_att = ctx_.rg->create_texture(
      {.format = TextureFormat::D32float,
       .dims = {shadow_cfg_.shadow_map_resolution, shadow_cfg_.shadow_map_resolution},
       .array_layers = shadow_cfg_.max_cascades,
       .size_class = SizeClass::Custom},
      "meshlet_shadow_depth_att");
  RGResourceId shadow_depth_att_id{};
  RGResourceId depth_att_id{};

  uint32_t meshlet_flags = MESHLET_OCCLUSION_CULL_ENABLED_BIT;
  if (gpu_object_frustum_cull_) {
    meshlet_flags |= MESHLET_FRUSTUM_CULL_ENABLED_BIT;
  }
  // Temporary debug path: disable shadow-pass culling to isolate artifacts.
  const uint32_t shadow_meshlet_flags = 0u;

  if (shadow_frame_data.cascade_count > 0) {
    auto& p = ctx_.rg->add_graphics_pass("meshlet_shadow_cascades");
    for (uint32_t cascade_i = 0; cascade_i < shadow_frame_data.cascade_count; cascade_i++) {
      task_cmd_shadow_rg[cascade_i] = p.read_buf(
          task_cmd_shadow_rg[cascade_i], PipelineStage::MeshShader | PipelineStage::TaskShader);
      indirect_args_shadow_rg[cascade_i] =
          p.read_buf(indirect_args_shadow_rg[cascade_i],
                     PipelineStage::TaskShader | PipelineStage::DrawIndirect,
                     AccessFlags::IndirectCommandRead);
    }
    meshlet_vis_rg_id = p.rw_buf(meshlet_vis_rg_id, PipelineStage::TaskShader);
    meshlet_stats_rg = p.rw_buf(meshlet_stats_rg, PipelineStage::TaskShader);
    shadow_depth_att_id = p.write_depth_output(shadow_depth_att);

    const auto local_task_cmd_shadow_rg = task_cmd_shadow_rg;
    const auto local_indirect_args_shadow_rg = indirect_args_shadow_rg;
    const auto local_shadow_view_cbs = shadow_view_cbs;
    const auto local_shadow_cull_cbs = shadow_cull_cbs;
    p.set_ex([this, shadow_depth_att_id, local_task_cmd_shadow_rg, local_indirect_args_shadow_rg,
              local_shadow_view_cbs, local_shadow_cull_cbs, meshlet_vis_rg_id, meshlet_stats_rg,
              shadow_cascade_count = shadow_frame_data.cascade_count,
              shadow_globals_cb_buf](CmdEncoder* enc) {
      auto& geo_batch = ctx_.model_gpu_mgr->geometry_batch();
      const rhi::TextureHandle depth_img = ctx_.rg->get_att_img(shadow_depth_att_id);
      if (depth_img != cached_shadow_depth_tex_) {
        if (cached_shadow_depth_tex_.is_valid()) {
          for (auto& view : shadow_depth_layer_views_) {
            if (view >= 0) {
              ctx_.device->destroy(cached_shadow_depth_tex_, view);
              view = -1;
            }
          }
        }
        cached_shadow_depth_tex_ = depth_img;
        for (uint32_t cascade_i = 0; cascade_i < shadow_cfg_.max_cascades; cascade_i++) {
          shadow_depth_layer_views_[cascade_i] =
              ctx_.device->create_tex_view(depth_img, 0, 1, cascade_i, 1);
        }
      }

      const glm::uvec2 shadow_vp_dims{shadow_cfg_.shadow_map_resolution,
                                      shadow_cfg_.shadow_map_resolution};
      for (uint32_t cascade_i = 0; cascade_i < shadow_cascade_count; cascade_i++) {
        enc->begin_rendering({
            RenderAttInfo::depth_stencil_att(
                depth_img, LoadOp::Clear, ClearValue{.depth_stencil = {.depth = 1.f, .stencil = 0}},
                rhi::StoreOp::Store, shadow_depth_layer_views_[cascade_i]),
        });
        encode_meshlet_test_draw_pass(
            false, false, shadow_meshlet_flags, ctx_.device, *ctx_.rg, geo_batch,
            ctx_.model_gpu_mgr->materials_allocator().get_buffer_handle(), shadow_globals_cb_buf,
            local_shadow_view_cbs[cascade_i], local_shadow_cull_cbs[cascade_i],
            rhi::TextureHandle{}, shadow_vp_dims, meshlet_vis_rg_id, meshlet_stats_rg,
            local_task_cmd_shadow_rg[cascade_i],
            ctx_.rg->get_buf(local_indirect_args_shadow_rg[cascade_i]),
            ctx_.model_gpu_mgr->instance_mgr(), std::span(meshlet_pso_shadow_), enc);
        enc->end_rendering();
      }
    });
  }

  {
    auto& p = ctx_.rg->add_graphics_pass("meshlet_occlusion_early");
    task_cmd_early_rg =
        p.read_buf(task_cmd_early_rg, PipelineStage::MeshShader | PipelineStage::TaskShader);
    indirect_args_early_rg =
        p.read_buf(indirect_args_early_rg, PipelineStage::TaskShader | PipelineStage::DrawIndirect,
                   AccessFlags::IndirectCommandRead);
    meshlet_vis_rg_id = p.rw_buf(meshlet_vis_rg_id, PipelineStage::TaskShader);
    meshlet_stats_rg = p.rw_buf(meshlet_stats_rg, PipelineStage::TaskShader);
    gbuffer_a_id = p.write_color_output(gbuffer_a_id);
    gbuffer_b_id = p.write_color_output(gbuffer_b_id);
    depth_att_id = p.write_depth_output(depth_att);

    p.set_ex([this, task_cmd_early_rg, indirect_args_early_rg, depth_att_id, view_cb_suballoc,
              globals_cb_buf, gbuffer_a_id, gbuffer_b_id, meshlet_vis_rg_id, meshlet_stats_rg,
              meshlet_flags, cull_early_cb](CmdEncoder* enc) {
      glm::vec4 clear_color{0.06f, 0.07f, 0.09f, 1.f};
      const glm::uvec2 vp_dims{ctx_.swapchain->desc_.width, ctx_.swapchain->desc_.height};
      ctx_.device->enqueue_swapchain_for_present(ctx_.swapchain, enc);
      enc->begin_rendering({
          RenderAttInfo::color_att(ctx_.rg->get_att_img(gbuffer_a_id), LoadOp::Clear,
                                   ClearValue{.color = clear_color}),
          RenderAttInfo::color_att(ctx_.rg->get_att_img(gbuffer_b_id), LoadOp::Clear,
                                   ClearValue{.color = glm::vec4{0.f}}),
          RenderAttInfo::depth_stencil_att(
              ctx_.rg->get_att_img(depth_att_id), LoadOp::Clear,
              ClearValue{.depth_stencil = {.depth = reverse_z_ ? 0.f : 1.f, .stencil = 0}}),
      });

      auto& geo_batch = ctx_.model_gpu_mgr->geometry_batch();
      encode_meshlet_test_draw_pass(
          reverse_z_, false, meshlet_flags, ctx_.device, *ctx_.rg, geo_batch,
          ctx_.model_gpu_mgr->materials_allocator().get_buffer_handle(), globals_cb_buf,
          view_cb_suballoc, cull_early_cb, rhi::TextureHandle{}, vp_dims, meshlet_vis_rg_id,
          meshlet_stats_rg, task_cmd_early_rg, ctx_.rg->get_buf(indirect_args_early_rg),
          ctx_.model_gpu_mgr->instance_mgr(), std::span(meshlet_pso_early_), enc);

      enc->end_rendering();
    });
  }

  RGResourceId final_depth_pyramid_rg{};
  if (depth_pyramid_tex_.is_valid()) {
    RGResourceId depth_pyramid_rg =
        ctx_.rg->import_external_texture(depth_pyramid_tex_.handle, "meshlet_depth_pyramid");

    auto* dp_tex = ctx_.device->get_tex(depth_pyramid_tex_);
    const glm::uvec2 dp_dims{dp_tex->desc().dims};
    const auto mip_levels = static_cast<uint32_t>(math::get_mip_levels(dp_dims.x, dp_dims.y));
    uint32_t final_mip = mip_levels - 1;
    RGResourceId depth_pyramid_id = depth_pyramid_rg;
    const RGResourceId depth_src_rg = depth_att_id;

    for (uint32_t mip = 0; mip <= final_mip; mip++) {
      auto& p = ctx_.rg->add_compute_pass("meshlet_depth_reduce_" + std::to_string(mip));
      RGResourceId depth_handle{};
      if (mip == 0) {
        depth_handle = p.sample_tex(depth_src_rg, rhi::PipelineStage::ComputeShader,
                                    RgSubresourceRange::single_mip(0));
      }
      if (mip == 0) {
        p.write_tex(depth_pyramid_id, rhi::PipelineStage::ComputeShader,
                    RgSubresourceRange::single_mip(mip));
      } else {
        depth_pyramid_id =
            p.rw_tex(depth_pyramid_id, rhi::PipelineStage::ComputeShader,
                     rhi::AccessFlags::ShaderSampledRead, rhi::AccessFlags::ShaderWrite,
                     RgSubresourceRange::single_mip(mip - 1), RgSubresourceRange::single_mip(mip));
      }
      if (mip == final_mip) {
        final_depth_pyramid_rg = depth_pyramid_id;
      }

      p.set_ex([this, mip, depth_handle, dp_dims](CmdEncoder* enc) {
        enc->bind_pipeline(depth_reduce_pso_);
        glm::uvec2 in_dims =
            (mip == 0) ? ctx_.device->get_tex(ctx_.rg->get_att_img(depth_handle))->desc().dims
                       : glm::uvec2{std::max(1u, dp_dims.x >> (mip - 1)),
                                    std::max(1u, dp_dims.y >> (mip - 1))};
        DepthReducePC pc{.in_tex_dim_x = in_dims.x,
                         .in_tex_dim_y = in_dims.y,
                         .out_tex_dim_x = dp_dims.x >> mip,
                         .out_tex_dim_y = dp_dims.y >> mip};
        enc->push_constants(&pc, sizeof(pc));

        if (mip == 0) {
          enc->bind_srv(ctx_.rg->get_att_img(depth_handle), 0);
        } else {
          enc->bind_srv(depth_pyramid_tex_.handle, 0,
                        depth_pyramid_tex_.views[static_cast<size_t>(mip - 1)]);
        }
        enc->bind_uav(depth_pyramid_tex_.handle, 0,
                      depth_pyramid_tex_.views[static_cast<size_t>(mip)]);

        constexpr size_t k_tg_size = 8;
        enc->dispatch_compute(glm::uvec3{align_divide_up(pc.out_tex_dim_x, k_tg_size),
                                         align_divide_up(pc.out_tex_dim_y, k_tg_size), 1},
                              glm::uvec3{k_tg_size, k_tg_size, 1});
      });
    }
  }

  generate_task_cmd_compute_pass_->bake(
      max_draws, true, gpu_object_frustum_cull_, gpu_object_occlusion_cull_, view_cb_suballoc,
      cull_late_cb, task_cmd_late_rg, indirect_args_late_rg, visible_object_count_rg,
      &instance_vis_current_rg, &final_depth_pyramid_rg, depth_pyramid_tex_.handle);

  {
    auto& p = ctx_.rg->add_graphics_pass("meshlet_occlusion_late");
    task_cmd_late_rg =
        p.read_buf(task_cmd_late_rg, PipelineStage::MeshShader | PipelineStage::TaskShader);
    indirect_args_late_rg =
        p.read_buf(indirect_args_late_rg, PipelineStage::TaskShader | PipelineStage::DrawIndirect,
                   AccessFlags::IndirectCommandRead);
    if (final_depth_pyramid_rg.is_valid()) {
      p.sample_tex(final_depth_pyramid_rg, rhi::PipelineStage::TaskShader,
                   RgSubresourceRange::all_mips_all_slices());
    }
    meshlet_vis_rg_id = p.rw_buf(meshlet_vis_rg_id, PipelineStage::TaskShader);
    meshlet_stats_rg = p.rw_buf(meshlet_stats_rg, PipelineStage::TaskShader);
    visible_object_count_rg = p.copy_from_buf(visible_object_count_rg);
    gbuffer_a_id = p.rw_color_output(gbuffer_a_id);
    gbuffer_b_id = p.rw_color_output(gbuffer_b_id);
    depth_att_id = p.rw_depth_output(depth_att);

    p.set_ex([this, task_cmd_late_rg, indirect_args_late_rg, depth_att_id, view_cb_suballoc,
              globals_cb_buf, gbuffer_a_id, gbuffer_b_id, meshlet_vis_rg_id, meshlet_stats_rg,
              meshlet_flags, cull_late_cb](CmdEncoder* enc) {
      const glm::uvec2 vp_dims{ctx_.swapchain->desc_.width, ctx_.swapchain->desc_.height};
      enc->begin_rendering({
          RenderAttInfo::color_att(ctx_.rg->get_att_img(gbuffer_a_id), LoadOp::Load,
                                   ClearValue{.color = glm::vec4{0.f}}),
          RenderAttInfo::color_att(ctx_.rg->get_att_img(gbuffer_b_id), LoadOp::Load,
                                   ClearValue{.color = glm::vec4{0.f}}),
          RenderAttInfo::depth_stencil_att(
              ctx_.rg->get_att_img(depth_att_id), LoadOp::Load,
              ClearValue{.depth_stencil = {.depth = reverse_z_ ? 0.f : 1.f, .stencil = 0}}),
      });

      auto& geo_batch = ctx_.model_gpu_mgr->geometry_batch();
      encode_meshlet_test_draw_pass(
          reverse_z_, true, meshlet_flags, ctx_.device, *ctx_.rg, geo_batch,
          ctx_.model_gpu_mgr->materials_allocator().get_buffer_handle(), globals_cb_buf,
          view_cb_suballoc, cull_late_cb, depth_pyramid_tex_.handle, vp_dims, meshlet_vis_rg_id,
          meshlet_stats_rg, task_cmd_late_rg, ctx_.rg->get_buf(indirect_args_late_rg),
          ctx_.model_gpu_mgr->instance_mgr(), std::span(meshlet_pso_late_), enc);

      enc->end_rendering();
    });
  }

  // Readback the early indirect arg count (opaque dispatch x = task group count, debug stat).
  add_buffer_readback_copy2(
      *ctx_.rg, "readback_task_cmd_group_count", indirect_args_early_rg,
      ctx_.rg->import_external_buffer(
          task_cmd_group_count_readback_[ctx_.curr_frame_in_flight_idx].handle,
          "task_cmd_group_count_readback"),
      0, 0, sizeof(uint32_t));
  add_buffer_readback_copy2(
      *ctx_.rg, "readback_visible_object_count", visible_object_count_rg,
      ctx_.rg->import_external_buffer(
          visible_object_count_readback_[ctx_.curr_frame_in_flight_idx].handle,
          "visible_object_count_readback"),
      0, 0, sizeof(uint32_t));

  add_buffer_readback_copy2(*ctx_.rg, "readback_meshlet_draw_stats", meshlet_stats_rg,
                            ctx_.rg->import_external_buffer(
                                meshlet_stats_buf_readback_[ctx_.curr_frame_in_flight_idx].handle,
                                "meshlet_draw_stats_readback"),
                            0, 0, k_meshlet_draw_stats_bytes);

  const bool depth_reduce_ran = final_depth_pyramid_rg.is_valid();

  {
    auto& p = ctx_.rg->add_graphics_pass("shade");
    gbuffer_a_id = p.sample_tex(gbuffer_a_id, rhi::PipelineStage::FragmentShader,
                                RgSubresourceRange::single_mip(0));
    gbuffer_b_id = p.sample_tex(gbuffer_b_id, rhi::PipelineStage::FragmentShader,
                                RgSubresourceRange::single_mip(0));
    depth_att_id = p.sample_tex(depth_att_id, rhi::PipelineStage::FragmentShader,
                                RgSubresourceRange::single_mip(0));
    if (shadow_depth_att_id.is_valid()) {
      shadow_depth_att_id =
          p.sample_tex(shadow_depth_att_id, rhi::PipelineStage::FragmentShader,
                       RgSubresourceRange::mip_layers(0, 1, 0, shadow_cfg_.max_cascades));
    }
    if (depth_reduce_ran) {
      p.sample_tex(final_depth_pyramid_rg, rhi::PipelineStage::FragmentShader,
                   RgSubresourceRange::all_mips_all_slices());
    }
    p.w_swapchain_tex(ctx_.swapchain);
    p.set_ex([this, gbuffer_a_id, gbuffer_b_id, depth_att_id, shadow_depth_att_id, depth_reduce_ran,
              shadow_csm_cb, globals_cb_buf, view_cb_suballoc](CmdEncoder* enc) {
      enc->begin_rendering({
          RenderAttInfo::color_att(ctx_.swapchain->get_current_texture(), LoadOp::DontCare),
      });
      enc->bind_pipeline(shade_pso_);
      enc->bind_cbv(globals_cb_buf.buf, GLOBALS_SLOT, globals_cb_buf.offset_bytes,
                    sizeof(GlobalData));
      enc->bind_cbv(view_cb_suballoc.buf, VIEW_DATA_SLOT, view_cb_suballoc.offset_bytes,
                    sizeof(ViewData));
      enc->bind_cbv(shadow_csm_cb.buf, 4, shadow_csm_cb.offset_bytes, sizeof(CSMData));
      enc->set_wind_order(rhi::WindOrder::CounterClockwise);
      enc->set_cull_mode(rhi::CullMode::None);
      glm::uvec2 dims = {ctx_.swapchain->desc_.width, ctx_.swapchain->desc_.height};
      enc->set_viewport({0, 0}, dims);
      enc->set_scissor({0, 0}, dims);

      const uint32_t gbuffer_a_bindless =
          ctx_.device->get_tex(ctx_.rg->get_att_img(gbuffer_a_id))->bindless_idx();
      const uint32_t gbuffer_b_bindless =
          ctx_.device->get_tex(ctx_.rg->get_att_img(gbuffer_b_id))->bindless_idx();
      const uint32_t depth_bindless =
          ctx_.device->get_tex(ctx_.rg->get_att_img(depth_att_id))->bindless_idx();
      uint32_t shadow_bindless = UINT32_MAX;
      if (shadow_depth_att_id.is_valid()) {
        shadow_bindless =
            ctx_.device->get_tex(ctx_.rg->get_att_img(shadow_depth_att_id))->bindless_idx();
      }
      uint32_t pyramid_view_bindless = UINT32_MAX;
      glm::uvec2 pyramid_base{0, 0};
      if (depth_pyramid_tex_.is_valid() && depth_reduce_ran) {
        const int mip = std::clamp(debug_depth_pyramid_mip_, 0,
                                   static_cast<int>(depth_pyramid_tex_.views.size()) - 1);
        pyramid_view_bindless = ctx_.device->get_tex_view_bindless_idx(
            depth_pyramid_tex_.handle, depth_pyramid_tex_.views[static_cast<size_t>(mip)]);
        pyramid_base = glm::uvec2{ctx_.device->get_tex(depth_pyramid_tex_)->desc().dims};
      }
      MeshletShadePC shade_pc{
          .gbuffer_a_idx = gbuffer_a_bindless,
          .gbuffer_b_idx = gbuffer_b_bindless,
          .depth_idx = depth_bindless,
          .shadow_depth_array_idx = shadow_bindless,
          .depth_pyramid_view_idx = pyramid_view_bindless,
          .swap_w = dims.x,
          .swap_h = dims.y,
          .pyramid_base_w = pyramid_base.x,
          .pyramid_base_h = pyramid_base.y,
      };
      enc->push_constants(&shade_pc, sizeof(shade_pc));
      enc->draw_primitives(rhi::PrimitiveTopology::TriangleList, 3);

      if (ctx_.imgui_ui_active && ctx_.imgui_renderer != nullptr) {
        ctx_.imgui_renderer->render(enc,
                                    {ctx_.swapchain->desc_.width, ctx_.swapchain->desc_.height},
                                    ctx_.curr_frame_in_flight_idx);
      }
      enc->end_rendering();
    });
  }
}

GenerateTaskCmdComputePass::GenerateTaskCmdComputePass(rhi::Device& device, RenderGraph& rg,
                                                       ModelGPUMgr& model_gpu_mgr,
                                                       ShaderManager& shader_mgr)
    : device_(device), rg_(rg), model_gpu_mgr_(model_gpu_mgr) {
  prepare_meshlets_pso_ = shader_mgr.create_compute_pipeline(
      {.path = "debug_meshlet_prepare_meshlets", .type = rhi::ShaderType::Compute});
  prepare_meshlets_late_pso_ = shader_mgr.create_compute_pipeline(
      {.path = "debug_meshlet_prepare_meshlets_late", .type = rhi::ShaderType::Compute});
}

void GenerateTaskCmdComputePass::bake(
    uint32_t max_draws, bool late, bool gpu_object_frustum_cull, bool gpu_object_occlusion_cull,
    const BufferSuballoc& view_cb_suballoc, const BufferSuballoc& cull_cb,
    RGResourceId& task_cmd_rg, RGResourceId& indirect_args_rg,
    RGResourceId& visible_object_count_rg, RGResourceId* instance_vis_current_rg,
    RGResourceId* final_depth_pyramid_rg, rhi::TextureHandle final_depth_pyramid_tex) {
  auto& p = rg_.add_compute_pass(late ? "meshlet_prepare_meshlets_late"
                                      : "meshlet_prepare_meshlets_early");
  task_cmd_rg = p.write_buf(task_cmd_rg, PipelineStage::ComputeShader);
  indirect_args_rg = p.rw_buf(indirect_args_rg, PipelineStage::ComputeShader);
  visible_object_count_rg = p.rw_buf(visible_object_count_rg, PipelineStage::ComputeShader);
  if (gpu_object_occlusion_cull) {
    if (late) {
      *instance_vis_current_rg = p.rw_buf(*instance_vis_current_rg, PipelineStage::ComputeShader);
      p.sample_tex(*final_depth_pyramid_rg, rhi::PipelineStage::ComputeShader,
                   RgSubresourceRange::all_mips_all_slices());
    } else {
      p.read_buf(*instance_vis_current_rg, PipelineStage::ComputeShader,
                 rhi::AccessFlags::ShaderStorageRead);
    }
  }

  const RGResourceId instance_vis_current_id =
      instance_vis_current_rg != nullptr ? *instance_vis_current_rg : RGResourceId{};
  p.set_ex([this, task_cmd_rg, indirect_args_rg, visible_object_count_rg, max_draws,
            gpu_object_frustum_cull, gpu_object_occlusion_cull, view_cb_suballoc, cull_cb, late,
            final_depth_pyramid_tex, instance_vis_current_id](CmdEncoder* enc) {
    enc->bind_pipeline(late ? prepare_meshlets_late_pso_ : prepare_meshlets_pso_);
    DebugMeshletPreparePC pc{
        .dst_task_cmd_buf_idx = device_.get_buf(rg_.get_buf(task_cmd_rg))->bindless_idx(),
        .taskcmd_cnt_buf_idx = device_.get_buf(rg_.get_buf(indirect_args_rg))->bindless_idx(),
        .instance_data_buf_idx =
            device_.get_buf(model_gpu_mgr_.instance_mgr().get_instance_data_buf())->bindless_idx(),
        .mesh_data_buf_idx =
            device_.get_buf(model_gpu_mgr_.geometry_batch().mesh_buf.get_buffer_handle())
                ->bindless_idx(),
        .max_draws = max_draws,
        .flags =
            (gpu_object_frustum_cull ? MESHLET_PREPARE_OBJECT_FRUSTUM_CULL_ENABLED_BIT : 0u) |
            (gpu_object_occlusion_cull ? MESHLET_PREPARE_OBJECT_OCCLUSION_CULL_ENABLED_BIT : 0u),
        .visible_obj_cnt_buf_idx =
            device_.get_buf(rg_.get_buf(visible_object_count_rg))->bindless_idx(),
        .instance_vis_buf_idx =
            gpu_object_occlusion_cull
                ? device_.get_buf(rg_.get_buf(instance_vis_current_id))->bindless_idx()
                : UINT32_MAX,
        .depth_pyramid_tex_idx = gpu_object_occlusion_cull && late
                                     ? device_.get_tex(final_depth_pyramid_tex)->bindless_idx()
                                     : UINT32_MAX,
    };
    enc->bind_cbv(view_cb_suballoc.buf, VIEW_DATA_SLOT, view_cb_suballoc.offset_bytes,
                  sizeof(ViewData));
    enc->bind_cbv(cull_cb.buf, 4, cull_cb.offset_bytes, sizeof(CullData));
    enc->push_constants(&pc, sizeof(pc));
    enc->dispatch_compute({align_divide_up(static_cast<uint64_t>(max_draws), 64ull), 1, 1},
                          {64, 1, 1});
  });
}

}  // namespace teng::gfx
