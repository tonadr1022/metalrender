#include "MeshletRendererTestScene.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <numbers>
#include <span>
#include <string>

#include "MeshletTestRenderUtil.hpp"
#include "ResourceManager.hpp"
#include "Window.hpp"
#include "gfx/DrawBatch.hpp"
#include "gfx/ImGuiRenderer.hpp"
#include "gfx/ModelGPUManager.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/ShaderManager.hpp"
#include "gfx/rhi/Swapchain.hpp"
#include "gfx/rhi/Texture.hpp"
#include "hlsl/meshlet_test/shared_meshlet_test_shade.h"
#include "hlsl/shared_forward_meshlet.h"
#include "hlsl/shared_globals.h"
#include "hlsl/shared_meshlet_draw_stats.hlsli"
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
    : ITestScene(ctx),
      draw_prep_(*ctx_.device, *ctx_.rg, *ctx_.model_gpu_mgr, *ctx_.shader_mgr),
      depth_pyramid_(*ctx_.device, *ctx_.rg, *ctx_.shader_mgr),
      csm_renderer_(*ctx_.device, *ctx_.rg, *ctx_.model_gpu_mgr, *ctx_.shader_mgr),
      frame_uniform_gpu_allocator_(ctx_.device, true) {
  ASSERT(ctx_.model_gpu_mgr != nullptr);
  ASSERT(ctx_.shader_mgr != nullptr);
  ASSERT(ctx_.frame_staging != nullptr);

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
  }
  shade_pso_ = ctx_.shader_mgr->create_graphics_pipeline({
      .shaders = {{{"fullscreen_quad", ShaderType::Vertex},
                   {"meshlet_test/shade", ShaderType::Fragment}}},
      .name = "meshlet_test/shade",
  });
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

  if (preset.csm_defaults.has_value()) {
    const auto& d = *preset.csm_defaults;
    csm_renderer_.set_scene_defaults(d.z_near, d.z_far, d.cascade_count, d.split_lambda);
  } else {
    csm_renderer_.set_scene_defaults(MeshletCsmRenderer::SceneDefaults{});
  }

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
  depth_pyramid_.resize({ctx_.swapchain->desc_.width, ctx_.swapchain->desc_.height});
}

void MeshletRendererScene::shutdown() {
  if (ctx_.window) {
    fps_camera_.set_mouse_captured(ctx_.window->get_handle(), false);
  }
  csm_renderer_.shutdown();
  depth_pyramid_.shutdown();
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
  csm_renderer_.on_imgui();
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

  depth_pyramid_.add_debug_imgui();

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
  cd.projection_type = CULL_PROJECTION_PERSPECTIVE;
  return cd;
}

CullData MeshletRendererScene::prepare_cull_data(const ViewData& vd) const {
  return prepare_cull_data_for_proj(vd.proj, 0.1f, 10'000.f);
}

CullData MeshletRendererScene::prepare_cull_data_late(const ViewData& vd) const {
  CullData cd = prepare_cull_data(vd);
  if (depth_pyramid_.is_valid()) {
    const auto dims = depth_pyramid_.dims();
    cd.pyramid_width = dims.x;
    cd.pyramid_height = dims.y;
    cd.pyramid_mip_count = depth_pyramid_.mip_count();
  }
  return cd;
}

void MeshletRendererScene::add_render_graph_passes() {
  auto& batch = ctx_.model_gpu_mgr->geometry_batch();
  const size_t task_cmd_count = batch.task_cmd_count;
  // TODO: still need to render the ui, etc.
  if (task_cmd_count == 0 || batch.get_stats().vertex_count == 0) {
    return;
  }

  if (ctx_.swapchain) {
    make_depth_pyramid_tex();
  }
  ASSERT(depth_pyramid_.is_valid());

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
    gd.render_mode = csm_renderer_.visualize_cascade_colors() ? DEBUG_RENDER_MODE_CSM_CASCADE_COLORS
                                                              : DEBUG_RENDER_MODE_NONE;
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

  RGResourceId instance_vis_current_rg{};
  const uint32_t max_draws = ctx_.model_gpu_mgr->instance_mgr().stats().max_instance_data_count;
  if (gpu_object_occlusion_cull_) {
    instance_vis_current_rg =
        draw_prep_.create_instance_visibility_buffer(max_draws, "meshlet_test_instance_vis");
    draw_prep_.prime_instance_visibility(instance_vis_current_rg, max_draws,
                                         "meshlet_prime_instance_vis");
  }

  MeshletDrawPrep::PassBuffers early_draws =
      draw_prep_.create_pass_buffers("meshlet_hello_early", task_cmd_count);
  MeshletDrawPrep::PassBuffers late_draws = draw_prep_.create_pass_buffers(
      "meshlet_hello_late", task_cmd_count, early_draws.visible_object_count_rg);

  RGResourceId meshlet_stats_rg = ctx_.rg->create_buffer(
      {.size = k_meshlet_draw_stats_bytes, .defer_reuse = true}, "meshlet_test_draw_stats");

  draw_prep_.clear_visible_count_and_stats(early_draws.visible_object_count_rg, meshlet_stats_rg,
                                           k_meshlet_draw_stats_bytes,
                                           "meshlet_clear_visible_count_buf_and_stats");
  late_draws.visible_object_count_rg = early_draws.visible_object_count_rg;

  std::array<RGResourceId, 2> main_indirect_args{early_draws.indirect_args_rg,
                                                 late_draws.indirect_args_rg};
  draw_prep_.clear_indirect_args("meshlet_clear_indirect_mesh_cmds", std::span(main_indirect_args));
  early_draws.indirect_args_rg = main_indirect_args[0];
  late_draws.indirect_args_rg = main_indirect_args[1];

  draw_prep_.bake_task_commands(
      {
          .pass_name = "meshlet_prepare_meshlets_early",
          .max_draws = max_draws,
          .late = false,
          .object_frustum_cull = gpu_object_frustum_cull_,
          .object_occlusion_cull = gpu_object_occlusion_cull_,
          .view_cb = view_cb_suballoc,
          .cull_cb = cull_early_cb,
          .instance_vis_current_rg = &instance_vis_current_rg,
      },
      early_draws);
  late_draws.visible_object_count_rg = early_draws.visible_object_count_rg;

  RGResourceId gbuffer_a_id =
      ctx_.rg->create_texture({.format = rhi::TextureFormat::R16G16B16A16Sfloat}, "gbuffer_a");
  RGResourceId gbuffer_b_id =
      ctx_.rg->create_texture({.format = rhi::TextureFormat::R16G16B16A16Sfloat}, "gbuffer_b");
  RGResourceId depth_att = ctx_.rg->create_texture(
      {.format = TextureFormat::D32float, .size_class = SizeClass::Swapchain},
      "meshlet_hello_depth_att");
  RGResourceId depth_att_id{};

  uint32_t meshlet_flags = MESHLET_OCCLUSION_CULL_ENABLED_BIT;
  if (gpu_object_frustum_cull_) {
    meshlet_flags |= MESHLET_FRUSTUM_CULL_ENABLED_BIT;
  }
  auto shadow_output = csm_renderer_.bake({
      .camera_view = vd,
      .toward_light = toward_light,
      .shadow_globals_cb = shadow_globals_cb_buf,
      .max_draws = max_draws,
      .task_cmd_count = task_cmd_count,
      .meshlet_vis_rg = meshlet_vis_rg_id,
      .meshlet_stats_rg = meshlet_stats_rg,
      .frame_uniform_allocator = frame_uniform_gpu_allocator_,
      .draw_prep = draw_prep_,
  });

  {
    auto& p = ctx_.rg->add_graphics_pass("meshlet_occlusion_early");
    early_draws.task_cmd_rg =
        p.read_buf(early_draws.task_cmd_rg, PipelineStage::MeshShader | PipelineStage::TaskShader);
    early_draws.indirect_args_rg = p.read_buf(
        early_draws.indirect_args_rg, PipelineStage::TaskShader | PipelineStage::DrawIndirect,
        AccessFlags::IndirectCommandRead);
    meshlet_vis_rg_id = p.rw_buf(meshlet_vis_rg_id, PipelineStage::TaskShader);
    meshlet_stats_rg = p.rw_buf(meshlet_stats_rg, PipelineStage::TaskShader);
    gbuffer_a_id = p.write_color_output(gbuffer_a_id);
    gbuffer_b_id = p.write_color_output(gbuffer_b_id);
    depth_att_id = p.write_depth_output(depth_att);

    p.set_ex([this, early_draws, depth_att_id, view_cb_suballoc, globals_cb_buf, gbuffer_a_id,
              gbuffer_b_id, meshlet_vis_rg_id, meshlet_stats_rg, meshlet_flags,
              cull_early_cb](CmdEncoder* enc) {
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
          meshlet_stats_rg, early_draws.task_cmd_rg, ctx_.rg->get_buf(early_draws.indirect_args_rg),
          ctx_.model_gpu_mgr->instance_mgr(), std::span(meshlet_pso_early_), enc);

      enc->end_rendering();
    });
  }

  RGResourceId final_depth_pyramid_rg =
      depth_pyramid_.bake(depth_att_id, "meshlet_depth_pyramid", "meshlet_depth_reduce_");

  draw_prep_.bake_task_commands(
      {
          .pass_name = "meshlet_prepare_meshlets_late",
          .max_draws = max_draws,
          .late = true,
          .object_frustum_cull = gpu_object_frustum_cull_,
          .object_occlusion_cull = gpu_object_occlusion_cull_,
          .view_cb = view_cb_suballoc,
          .cull_cb = cull_late_cb,
          .instance_vis_current_rg = &instance_vis_current_rg,
          .final_depth_pyramid_rg = &final_depth_pyramid_rg,
          .final_depth_pyramid_tex = depth_pyramid_.texture(),
      },
      late_draws);

  {
    auto& p = ctx_.rg->add_graphics_pass("meshlet_occlusion_late");
    late_draws.task_cmd_rg =
        p.read_buf(late_draws.task_cmd_rg, PipelineStage::MeshShader | PipelineStage::TaskShader);
    late_draws.indirect_args_rg = p.read_buf(
        late_draws.indirect_args_rg, PipelineStage::TaskShader | PipelineStage::DrawIndirect,
        AccessFlags::IndirectCommandRead);
    if (final_depth_pyramid_rg.is_valid()) {
      p.sample_tex(final_depth_pyramid_rg, rhi::PipelineStage::TaskShader,
                   RgSubresourceRange::all_mips_all_slices());
    }
    meshlet_vis_rg_id = p.rw_buf(meshlet_vis_rg_id, PipelineStage::TaskShader);
    meshlet_stats_rg = p.rw_buf(meshlet_stats_rg, PipelineStage::TaskShader);
    late_draws.visible_object_count_rg = p.copy_from_buf(late_draws.visible_object_count_rg);
    gbuffer_a_id = p.rw_color_output(gbuffer_a_id);
    gbuffer_b_id = p.rw_color_output(gbuffer_b_id);
    depth_att_id = p.rw_depth_output(depth_att);

    p.set_ex([this, late_draws, depth_att_id, view_cb_suballoc, globals_cb_buf, gbuffer_a_id,
              gbuffer_b_id, meshlet_vis_rg_id, meshlet_stats_rg, meshlet_flags,
              cull_late_cb](CmdEncoder* enc) {
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
          view_cb_suballoc, cull_late_cb, depth_pyramid_.texture(), vp_dims, meshlet_vis_rg_id,
          meshlet_stats_rg, late_draws.task_cmd_rg, ctx_.rg->get_buf(late_draws.indirect_args_rg),
          ctx_.model_gpu_mgr->instance_mgr(), std::span(meshlet_pso_late_), enc);

      enc->end_rendering();
    });
  }

  // Readback the early indirect arg count (opaque dispatch x = task group count, debug stat).
  add_buffer_readback_copy2(
      *ctx_.rg, "readback_task_cmd_group_count", early_draws.indirect_args_rg,
      ctx_.rg->import_external_buffer(
          task_cmd_group_count_readback_[ctx_.curr_frame_in_flight_idx].handle,
          "task_cmd_group_count_readback"),
      0, 0, sizeof(uint32_t));
  add_buffer_readback_copy2(
      *ctx_.rg, "readback_visible_object_count", late_draws.visible_object_count_rg,
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
    if (shadow_output.valid) {
      shadow_output.depth_rg =
          p.sample_tex(shadow_output.depth_rg, rhi::PipelineStage::FragmentShader,
                       RgSubresourceRange::mip_layers(0, 1, 0, shadow_output.sample_layer_count));
    }
    if (depth_reduce_ran) {
      p.sample_tex(final_depth_pyramid_rg, rhi::PipelineStage::FragmentShader,
                   RgSubresourceRange::all_mips_all_slices());
    }
    p.w_swapchain_tex(ctx_.swapchain);
    p.set_ex([this, gbuffer_a_id, gbuffer_b_id, depth_att_id, shadow_output, depth_reduce_ran,
              globals_cb_buf, view_cb_suballoc](CmdEncoder* enc) {
      enc->begin_rendering({
          RenderAttInfo::color_att(ctx_.swapchain->get_current_texture(), LoadOp::DontCare),
      });
      enc->bind_pipeline(shade_pso_);
      enc->bind_cbv(globals_cb_buf.buf, GLOBALS_SLOT, globals_cb_buf.offset_bytes,
                    sizeof(GlobalData));
      enc->bind_cbv(view_cb_suballoc.buf, VIEW_DATA_SLOT, view_cb_suballoc.offset_bytes,
                    sizeof(ViewData));
      enc->bind_cbv(shadow_output.csm_cb.buf, 4, shadow_output.csm_cb.offset_bytes,
                    sizeof(CSMData));
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
      if (shadow_output.valid) {
        shadow_bindless =
            ctx_.device->get_tex(ctx_.rg->get_att_img(shadow_output.depth_rg))->bindless_idx();
      }
      uint32_t pyramid_view_bindless = UINT32_MAX;
      glm::uvec2 pyramid_base{0, 0};
      if (depth_pyramid_.is_valid() && depth_reduce_ran) {
        pyramid_view_bindless = depth_pyramid_.debug_view_bindless_idx();
        pyramid_base = depth_pyramid_.dims();
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

}  // namespace teng::gfx
