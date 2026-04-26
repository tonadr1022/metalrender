#include "gfx/renderer/MeshletRenderer.hpp"

#include <cmath>
#include <optional>
#include <span>
#include <string>

#include "engine/render/RenderFrameContext.hpp"
#include "engine/render/RenderScene.hpp"
#include "gfx/DrawBatch.hpp"
#include "gfx/ModelGPUManager.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/ShaderManager.hpp"
#include "gfx/renderer/MeshletDepthPyramid.hpp"
#include "gfx/renderer/MeshletDrawPrep.hpp"
#include "gfx/renderer/MeshletTestRenderUtil.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/Swapchain.hpp"
#include "gfx/rhi/Texture.hpp"
#include "hlsl/meshlet_test/shared_meshlet_test_shade.h"
#include "hlsl/shared_forward_meshlet.h"
#include "hlsl/shared_meshlet_draw_stats.hlsli"
#include "imgui.h"

namespace teng::gfx {

using namespace teng::gfx::rhi;

namespace {

const char* gpu_adapter_kind_str(rhi::GpuAdapterKind k) {
  switch (k) {
    case rhi::GpuAdapterKind::Integrated:
      return "Integrated";
    case rhi::GpuAdapterKind::Discrete:
      return "Discrete";
    case rhi::GpuAdapterKind::Virtual:
      return "Virtual";
    case rhi::GpuAdapterKind::Cpu:
      return "CPU";
    case rhi::GpuAdapterKind::Other:
      return "Other";
    case rhi::GpuAdapterKind::Unknown:
    default:
      return "Unknown";
  }
}

glm::mat4 infinite_perspective_proj(float fov_y, float aspect, float z_near) {
  // clang-format off
  const float f = 1.0f / tanf(fov_y / 2.0f);
  return {
    f / aspect, 0.0f, 0.0f, 0.0f,
    0.0f, f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, -1.0f,
    0.0f, 0.0f, z_near, 0.0f};
  // clang-format on
}

glm::vec3 safe_normalize_toward_light(const glm::vec3& v) {
  const float s2 = glm::dot(v, v);
  if (s2 < 1e-12f) {
    return glm::normalize(glm::vec3(0.35f, 1.f, 0.4f));
  }
  return v * (1.f / std::sqrt(s2));
}

const engine::RenderCamera* pick_camera(const engine::RenderScene& scene) {
  for (const auto& c : scene.cameras) {
    if (c.primary) {
      return &c;
    }
  }
  if (!scene.cameras.empty()) {
    return &scene.cameras.front();
  }
  return nullptr;
}

glm::vec3 directional_toward_light_unit_ws(const engine::RenderScene& scene) {
  glm::vec3 raw(0.35f, 1.f, 0.4f);
  if (!scene.directional_lights.empty()) {
    raw = scene.directional_lights.front().direction;
  }
  return safe_normalize_toward_light(raw);
}

std::optional<ViewData> build_view_data_for_camera(const engine::RenderCamera& cam,
                                                   glm::uvec2 extent_primary,
                                                   glm::uvec2 extent_fallback) {
  glm::uvec2 ext = extent_primary;
  if (ext.x == 0 || ext.y == 0) {
    ext = extent_fallback;
  }
  if (ext.x == 0 || ext.y == 0) {
    return std::nullopt;
  }
  const float aspect = static_cast<float>(ext.x) / std::max(1.f, static_cast<float>(ext.y));
  const float fov_y = cam.fov_y > 1e-6f ? cam.fov_y : glm::radians(60.f);
  const float z_near = cam.z_near > 0.f ? cam.z_near : 0.1f;
  const glm::mat4 proj = infinite_perspective_proj(fov_y, aspect, z_near);
  const glm::mat4 view = glm::inverse(cam.local_to_world);
  const glm::mat4 vp = proj * view;

  ViewData vd{};
  vd.vp = vp;
  vd.inv_vp = glm::inverse(vp);
  vd.view = view;
  vd.proj = proj;
  vd.inv_proj = glm::inverse(proj);
  vd.camera_pos = cam.local_to_world * glm::vec4(0.f, 0.f, 0.f, 1.f);
  return vd;
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

}  // namespace

MeshletRenderer::MeshletRenderer() = default;

MeshletRenderer::~MeshletRenderer() { shutdown_gpu(); }

void MeshletRenderer::lazy_init(const engine::RenderFrameContext& frame) {
  if (gpu_initialized_) {
    return;
  }
  ASSERT(frame.device != nullptr);
  ASSERT(frame.render_graph != nullptr);
  ASSERT(frame.model_gpu_mgr != nullptr);
  ASSERT(frame.shader_mgr != nullptr);
  ASSERT(frame.frame_staging != nullptr);

  gpu_device_ = frame.device;

  draw_prep_ = std::make_unique<MeshletDrawPrep>(*frame.device, *frame.render_graph,
                                                 *frame.model_gpu_mgr, *frame.shader_mgr);
  depth_pyramid_ =
      std::make_unique<MeshletDepthPyramid>(*frame.device, *frame.render_graph, *frame.shader_mgr);
  csm_renderer_ = std::make_unique<MeshletCsmRenderer>(*frame.device, *frame.render_graph,
                                                       *frame.model_gpu_mgr, *frame.shader_mgr);

  MeshletCsmRenderer::SceneDefaults defaults{};
  csm_renderer_->set_scene_defaults(defaults);

  for (size_t a = 0; a < static_cast<size_t>(AlphaMaskType::Count); ++a) {
    meshlet_pso_early_[a] = frame.shader_mgr->create_graphics_pipeline({
        .shaders = {{{"forward_meshlet", ShaderType::Task},
                     {"debug_meshlet_hello", ShaderType::Mesh},
                     {"debug_meshlet_hello", ShaderType::Fragment}}},
        .name = std::string("meshlet_test_forward_early_") + std::to_string(a),
    });
    meshlet_pso_late_[a] = frame.shader_mgr->create_graphics_pipeline({
        .shaders = {{{"forward_meshlet_late", ShaderType::Task},
                     {"debug_meshlet_hello", ShaderType::Mesh},
                     {"debug_meshlet_hello", ShaderType::Fragment}}},
        .name = std::string("meshlet_test_forward_late_") + std::to_string(a),
    });
  }
  shade_pso_ = frame.shader_mgr->create_graphics_pipeline({
      .shaders = {{{"fullscreen_quad", ShaderType::Vertex},
                   {"meshlet_test/shade", ShaderType::Fragment}}},
      .name = "meshlet_test/shade",
  });
  for (int i = 0; i < k_max_frames_in_flight; i++) {
    task_cmd_group_count_readback_[static_cast<size_t>(i)] = frame.device->create_buf_h({
        .size = sizeof(uint32_t),
        .flags = rhi::BufferDescFlags::CPUAccessible,
        .name = "meshlet_draw_count_readback",
    });
    visible_object_count_readback_[static_cast<size_t>(i)] = frame.device->create_buf_h({
        .size = sizeof(uint32_t),
        .flags = rhi::BufferDescFlags::CPUAccessible,
        .name = "meshlet_visible_object_count_readback",
    });
    meshlet_stats_buf_readback_[static_cast<size_t>(i)] = frame.device->create_buf_h({
        .size = k_meshlet_draw_stats_bytes,
        .flags = rhi::BufferDescFlags::CPUAccessible,
        .name = "meshlet_draw_stats_readback",
    });
  }

  frame_uniform_gpu_allocator_.emplace(frame.device, true);

  make_depth_pyramid_tex(frame);
  gpu_initialized_ = true;
}

void MeshletRenderer::shutdown_subsystems() {
  if (csm_renderer_) {
    csm_renderer_->shutdown();
    csm_renderer_.reset();
  }
  if (depth_pyramid_) {
    depth_pyramid_->shutdown();
    depth_pyramid_.reset();
  }
  draw_prep_.reset();
  frame_uniform_gpu_allocator_.reset();
  gpu_initialized_ = false;
}

void MeshletRenderer::shutdown_gpu() {
  rhi::Device* device = gpu_device_;
  if (gpu_initialized_) {
    shutdown_subsystems();
  }
  if (device != nullptr) {
    meshlet_vis_buf_ = {};
    for (auto& b : task_cmd_group_count_readback_) {
      b = {};
    }
    for (auto& b : visible_object_count_readback_) {
      b = {};
    }
    for (auto& b : meshlet_stats_buf_readback_) {
      b = {};
    }
  }
  shade_pso_ = {};
  for (auto& h : meshlet_pso_early_) {
    h = {};
  }
  for (auto& h : meshlet_pso_late_) {
    h = {};
  }
  gpu_device_ = nullptr;
}

void MeshletRenderer::make_depth_pyramid_tex(const engine::RenderFrameContext& frame) {
  if (!depth_pyramid_) {
    return;
  }
  glm::uvec2 dims = frame.output_extent;
  if (dims.x == 0 || dims.y == 0) {
    if (frame.swapchain != nullptr) {
      dims = {frame.swapchain->desc_.width, frame.swapchain->desc_.height};
    }
  }
  if (dims.x == 0 || dims.y == 0) {
    return;
  }
  depth_pyramid_->resize(dims);
}

void MeshletRenderer::on_resize(engine::RenderFrameContext& frame) {
  make_depth_pyramid_tex(frame);
}

void MeshletRenderer::on_imgui(engine::RenderFrameContext&) { imgui_gpu_panels(); }

void MeshletRenderer::bake_swapchain_clear(engine::RenderFrameContext& frame,
                                           std::string_view pass_name) {
  ASSERT(frame.swapchain != nullptr);
  const glm::vec4 clear_color{0.06f, 0.07f, 0.09f, 1.f};
  auto& p = frame.render_graph->add_graphics_pass(pass_name);
  frame.curr_swapchain_rg_id = p.w_swapchain_tex_new(frame.swapchain, frame.curr_swapchain_rg_id);
  p.set_ex([swapchain = frame.swapchain, clear_color](CmdEncoder* enc) {
    enc->begin_rendering({
        RenderAttInfo::color_att(swapchain->get_current_texture(), LoadOp::Clear,
                                 ClearValue{.color = clear_color}),
    });
    enc->end_rendering();
  });
}

CullData MeshletRenderer::prepare_cull_data_for_proj(const glm::mat4& proj, float z_near,
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

CullData MeshletRenderer::prepare_cull_data_late(const ViewData& vd, float z_near,
                                                 float z_far) const {
  CullData cd = prepare_cull_data_for_proj(vd.proj, z_near, z_far);
  if (depth_pyramid_ && depth_pyramid_->is_valid()) {
    const auto dims = depth_pyramid_->dims();
    cd.pyramid_width = dims.x;
    cd.pyramid_height = dims.y;
    cd.pyramid_mip_count = depth_pyramid_->mip_count();
  }
  return cd;
}

void MeshletRenderer::imgui_gpu_panels() {
  if (!last_imgui_frame_.has_value() || last_imgui_frame_->device == nullptr) {
    return;
  }
  const MeshletImguiFrameSnapshot& snap = *last_imgui_frame_;

  ImGui::Checkbox("GPU object frustum cull", &gpu_object_frustum_cull_);
  ImGui::Checkbox("GPU object occlusion cull", &gpu_object_occlusion_cull_);
  if (csm_renderer_) {
    csm_renderer_->on_imgui();
  }
  uint32_t visible_meshlet_task_groups = 0;
  uint32_t visible_objects = 0;
  MeshletDrawStats visible_meshlet_stats{};
  if (frame_num_ >= snap.frames_in_flight) {
    const uint32_t curr = snap.curr_frame_in_flight_idx;
    visible_meshlet_task_groups = *reinterpret_cast<const uint32_t*>(
        snap.device->get_buf(task_cmd_group_count_readback_[curr].handle)->contents());
    visible_objects = *reinterpret_cast<const uint32_t*>(
        snap.device->get_buf(visible_object_count_readback_[curr].handle)->contents());
    visible_meshlet_stats = *reinterpret_cast<const MeshletDrawStats*>(
        snap.device->get_buf(meshlet_stats_buf_readback_[curr].handle)->contents());
  }
  ImGui::Text("Visible mesh task groups (GPU): %u", visible_meshlet_task_groups);
  ImGui::Text("Visible objects (GPU): %u", visible_objects);
  ImGui::Text("Visible meshlets (GPU): %u", visible_meshlet_stats.meshlets_drawn_early +
                                                visible_meshlet_stats.meshlets_drawn_late);
  ImGui::Text("Visible triangles (GPU): %u", visible_meshlet_stats.triangles_drawn_early +
                                                 visible_meshlet_stats.triangles_drawn_late);

  if (depth_pyramid_) {
    depth_pyramid_->add_debug_imgui();
  }

  {
    const rhi::GpuAdapterInfo info = gpu_device_->query_gpu_adapter_info();
    ImGui::Separator();
    ImGui::TextUnformatted("GPU / adapter");
    if (!info.name.empty()) {
      ImGui::TextWrapped("Name: %s", info.name.c_str());
    } else {
      ImGui::TextUnformatted("Name: (unavailable)");
    }
    ImGui::Text("Kind: %s", gpu_adapter_kind_str(info.kind));
    if (!info.api_version.empty()) {
      ImGui::Text("API: %s", info.api_version.c_str());
    }
    if (!info.driver_version.empty()) {
      ImGui::Text("Driver: %s", info.driver_version.c_str());
    }
    if (info.vendor_id != 0 || info.device_id != 0) {
      ImGui::Text("Vendor ID: 0x%08X  Device ID: 0x%08X", info.vendor_id, info.device_id);
    }
  }
}

void MeshletRenderer::render(engine::RenderFrameContext& frame, const engine::RenderScene& scene) {
  lazy_init(frame);
  last_imgui_frame_ = MeshletImguiFrameSnapshot{
      .device = frame.device,
      .curr_frame_in_flight_idx = frame.curr_frame_in_flight_idx,
      .frames_in_flight = static_cast<uint32_t>(frame.device->get_info().frames_in_flight),
  };

  {
    auto& static_instance_mgr = frame.model_gpu_mgr->instance_mgr();
    const uint32_t curr_frame_in_flight_idx = frame.curr_frame_in_flight_idx;
    if (static_instance_mgr.has_pending_frees(curr_frame_in_flight_idx)) {
      auto instance_data_id = frame.render_graph->import_external_buffer(
          static_instance_mgr.get_instance_data_buf(),
          RGState{.stage = PipelineStage::TopOfPipe, .layout = ResourceLayout::General},
          "instance_data_buf");
      auto& p = frame.render_graph->add_transfer_pass("free_instance_data");
      p.write_buf(instance_data_id, PipelineStage::AllTransfer);
      p.set_ex([&static_instance_mgr, curr_frame_in_flight_idx](CmdEncoder* enc) {
        static_instance_mgr.flush_pending_frees(curr_frame_in_flight_idx, enc);
      });
    }
  }
  ASSERT(frame.model_gpu_mgr != nullptr);
  frame.model_gpu_mgr->set_curr_frame_idx(frame.curr_frame_in_flight_idx);

  auto& static_instance_mgr = frame.model_gpu_mgr->instance_mgr();
  if (static_instance_mgr.has_pending_frees(frame.curr_frame_in_flight_idx)) {
    auto instance_data_id = frame.render_graph->import_external_buffer(
        static_instance_mgr.get_instance_data_buf(),
        RGState{.stage = PipelineStage::TopOfPipe, .layout = ResourceLayout::General},
        "instance_data_buf");
    auto& p = frame.render_graph->add_transfer_pass("free_instance_data");
    p.write_buf(instance_data_id, PipelineStage::AllTransfer);
    p.set_ex([&static_instance_mgr, fif = frame.curr_frame_in_flight_idx](CmdEncoder* enc) {
      static_instance_mgr.flush_pending_frees(fif, enc);
    });
  }

  auto& batch = frame.model_gpu_mgr->geometry_batch();
  const size_t task_cmd_count = batch.task_cmd_count;
  if (task_cmd_count == 0 || batch.get_stats().vertex_count == 0) {
    bake_swapchain_clear(frame, "meshlet_empty_scene_clear");
    return;
  }

  const engine::RenderCamera* active_cam = pick_camera(scene);
  if (active_cam == nullptr) {
    bake_swapchain_clear(frame, "meshlet_no_camera_clear");
    return;
  }

  const glm::uvec2 extent_primary = scene.frame.output_extent;
  const glm::uvec2 extent_fallback = frame.output_extent;
  const std::optional<ViewData> view_opt =
      build_view_data_for_camera(*active_cam, extent_primary, extent_fallback);
  if (!view_opt.has_value()) {
    bake_swapchain_clear(frame, "meshlet_bad_extent_clear");
    return;
  }

  const float z_near = active_cam->z_near > 0.f ? active_cam->z_near : 0.1f;
  const float z_far = active_cam->z_far > z_near ? active_cam->z_far : 10'000.f;
  const glm::vec3 toward_light = directional_toward_light_unit_ws(scene);

  if (frame.swapchain != nullptr) {
    make_depth_pyramid_tex(frame);
  }
  ASSERT(depth_pyramid_ != nullptr && depth_pyramid_->is_valid());

  bool meshlet_vis_buffer_reallocated = false;
  {
    const size_t n = frame.model_gpu_mgr->instance_mgr().get_num_meshlet_vis_buf_elements();
    const size_t need = n * sizeof(uint32_t);
    if (need == 0) {
      bake_swapchain_clear(frame, "meshlet_empty_vis_clear");
      return;
    }
    const rhi::Buffer* cur =
        meshlet_vis_buf_.handle.is_valid() ? frame.device->get_buf(meshlet_vis_buf_) : nullptr;
    if (cur == nullptr || cur->size() < need) {
      if (meshlet_vis_buf_.handle.is_valid()) {
        meshlet_vis_buf_ = {};
      }
      meshlet_vis_buf_ = frame.device->create_buf_h({
          .usage = rhi::BufferUsage::Storage,
          .size = need,
          .name = "meshlet_test_meshlet_vis",
      });
      meshlet_vis_buffer_reallocated = true;
    }
  }
  if (!meshlet_vis_buf_.handle.is_valid()) {
    bake_swapchain_clear(frame, "meshlet_invalid_vis_buf_clear");
    return;
  }

  const bool meshlet_vis_cleared_this_frame = (frame_num_ == 0 || meshlet_vis_buffer_reallocated);
  const RGState meshlet_vis_import_initial =
      meshlet_vis_cleared_this_frame
          ? RGState{}
          : RGState{.access = AccessFlags::ShaderRead | AccessFlags::ShaderWrite,
                    .stage = PipelineStage::TaskShader};
  RGResourceId meshlet_vis_rg_id = frame.render_graph->import_external_buffer(
      meshlet_vis_buf_.handle, meshlet_vis_import_initial, "meshlet_test_meshlet_vis_rg");

  if (meshlet_vis_cleared_this_frame) {
    const size_t need =
        frame.model_gpu_mgr->instance_mgr().get_num_meshlet_vis_buf_elements() * sizeof(uint32_t);
    auto& p = frame.render_graph->add_transfer_pass("meshlet_clear_meshlet_vis");
    meshlet_vis_rg_id = p.write_buf(meshlet_vis_rg_id, rhi::PipelineStage::AllTransfer);
    p.set_ex([need, this](rhi::CmdEncoder* enc) {
      enc->fill_buffer(meshlet_vis_buf_.handle, 0, static_cast<uint32_t>(need), 1);
    });
  }

  ASSERT(frame_uniform_gpu_allocator_.has_value());
  frame_uniform_gpu_allocator_->set_frame_idx_and_reset_bufs(frame.curr_frame_in_flight_idx);

  frame_num_++;

  ViewData vd = *view_opt;
  auto view_cb_suballoc = frame_uniform_gpu_allocator_->alloc2(sizeof(ViewData), &vd);

  auto cd_early = prepare_cull_data_for_proj(vd.proj, z_near, z_far);
  auto cull_early_cb = frame_uniform_gpu_allocator_->alloc2(sizeof(CullData), &cd_early);
  auto cd_late = prepare_cull_data_late(vd, z_near, z_far);
  auto cull_late_cb = frame_uniform_gpu_allocator_->alloc2(sizeof(CullData), &cd_late);

  BufferSuballoc globals_cb_buf;
  BufferSuballoc shadow_globals_cb_buf;
  {
    GlobalData gd{};
    gd.render_mode = csm_renderer_->visualize_cascade_colors()
                         ? DEBUG_RENDER_MODE_CSM_CASCADE_COLORS
                         : DEBUG_RENDER_MODE_NONE;
    gd.frame_num = frame_num_;
    gd.meshlet_stats_enabled = 1;
    gd._padding = 0;
    gd.diffuse_light_dir_world = glm::vec4(toward_light, 0.f);
    globals_cb_buf = frame_uniform_gpu_allocator_->alloc2(sizeof(GlobalData), &gd);

    GlobalData shadow_gd = gd;
    shadow_gd.meshlet_stats_enabled = 0;
    shadow_gd.render_mode = DEBUG_RENDER_MODE_NONE;
    shadow_globals_cb_buf = frame_uniform_gpu_allocator_->alloc2(sizeof(GlobalData), &shadow_gd);
  }

  RGResourceId instance_vis_current_rg{};
  const uint32_t max_draws = frame.model_gpu_mgr->instance_mgr().stats().max_instance_data_count;
  if (gpu_object_occlusion_cull_) {
    instance_vis_current_rg =
        draw_prep_->create_instance_visibility_buffer(max_draws, "meshlet_test_instance_vis");
    draw_prep_->prime_instance_visibility(instance_vis_current_rg, max_draws,
                                          "meshlet_prime_instance_vis");
  }

  MeshletDrawPrep::PassBuffers early_draws =
      draw_prep_->create_pass_buffers("meshlet_hello_early", task_cmd_count);
  MeshletDrawPrep::PassBuffers late_draws = draw_prep_->create_pass_buffers(
      "meshlet_hello_late", task_cmd_count, early_draws.visible_object_count_rg);

  RGResourceId meshlet_stats_rg = frame.render_graph->create_buffer(
      {.size = k_meshlet_draw_stats_bytes, .defer_reuse = true}, "meshlet_test_draw_stats");

  draw_prep_->clear_visible_count_and_stats(early_draws.visible_object_count_rg, meshlet_stats_rg,
                                            k_meshlet_draw_stats_bytes,
                                            "meshlet_clear_visible_count_buf_and_stats");
  late_draws.visible_object_count_rg = early_draws.visible_object_count_rg;

  std::array<RGResourceId, 2> main_indirect_args{early_draws.indirect_args_rg,
                                                 late_draws.indirect_args_rg};
  draw_prep_->clear_indirect_args("meshlet_clear_indirect_mesh_cmds",
                                  std::span(main_indirect_args));
  early_draws.indirect_args_rg = main_indirect_args[0];
  late_draws.indirect_args_rg = main_indirect_args[1];

  draw_prep_->bake_task_commands(
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

  RGResourceId gbuffer_a_id = frame.render_graph->create_texture(
      {.format = rhi::TextureFormat::R16G16B16A16Sfloat}, "gbuffer_a");
  RGResourceId gbuffer_b_id = frame.render_graph->create_texture(
      {.format = rhi::TextureFormat::R16G16B16A16Sfloat}, "gbuffer_b");
  const RGResourceId depth_att = frame.render_graph->create_texture(
      {.format = TextureFormat::D32float, .size_class = SizeClass::Swapchain},
      "meshlet_hello_depth_att");
  RGResourceId depth_att_id{};

  uint32_t meshlet_flags = MESHLET_OCCLUSION_CULL_ENABLED_BIT;
  if (gpu_object_frustum_cull_) {
    meshlet_flags |= MESHLET_FRUSTUM_CULL_ENABLED_BIT;
  }
  auto shadow_output = csm_renderer_->bake({
      .camera_view = vd,
      .toward_light = toward_light,
      .shadow_globals_cb = shadow_globals_cb_buf,
      .max_draws = max_draws,
      .task_cmd_count = task_cmd_count,
      .meshlet_vis_rg = meshlet_vis_rg_id,
      .meshlet_stats_rg = meshlet_stats_rg,
      .frame_uniform_allocator = *frame_uniform_gpu_allocator_,
      .draw_prep = *draw_prep_,
  });

  {
    auto& p = frame.render_graph->add_graphics_pass("meshlet_occlusion_early");
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

    RenderGraph* rg = frame.render_graph;
    rhi::Device* device = frame.device;
    ModelGPUMgr* model_gpu_mgr = frame.model_gpu_mgr;
    const glm::uvec2 out_ext = frame.output_extent;
    rhi::Swapchain* swapchain = frame.swapchain;
    p.set_ex([this, early_draws, depth_att_id, view_cb_suballoc, globals_cb_buf, gbuffer_a_id,
              gbuffer_b_id, meshlet_vis_rg_id, meshlet_stats_rg, meshlet_flags, cull_early_cb, rg,
              device, model_gpu_mgr, out_ext, swapchain](CmdEncoder* enc) {
      const glm::vec4 clear_color{0.06f, 0.07f, 0.09f, 1.f};
      const glm::uvec2 vp_u = (out_ext.x > 0 && out_ext.y > 0)
                                  ? out_ext
                                  : glm::uvec2{swapchain->desc_.width, swapchain->desc_.height};
      const glm::ivec2 vp_dims{static_cast<int>(vp_u.x), static_cast<int>(vp_u.y)};
      enc->begin_rendering({
          RenderAttInfo::color_att(rg->get_att_img(gbuffer_a_id), LoadOp::Clear,
                                   ClearValue{.color = clear_color}),
          RenderAttInfo::color_att(rg->get_att_img(gbuffer_b_id), LoadOp::Clear,
                                   ClearValue{.color = glm::vec4{0.f}}),
          RenderAttInfo::depth_stencil_att(
              rg->get_att_img(depth_att_id), LoadOp::Clear,
              ClearValue{.depth_stencil = {.depth = reverse_z_ ? 0.f : 1.f, .stencil = 0}}),
      });

      auto& geo_batch = model_gpu_mgr->geometry_batch();
      encode_meshlet_test_draw_pass(
          reverse_z_, false, meshlet_flags, device, *rg, geo_batch,
          model_gpu_mgr->materials_allocator().get_buffer_handle(), globals_cb_buf,
          view_cb_suballoc, cull_early_cb, rhi::TextureHandle{}, vp_dims, meshlet_vis_rg_id,
          meshlet_stats_rg, early_draws.task_cmd_rg, rg->get_buf(early_draws.indirect_args_rg),
          model_gpu_mgr->instance_mgr(), std::span(meshlet_pso_early_), enc);

      enc->end_rendering();
    });
  }

  RGResourceId final_depth_pyramid_rg =
      depth_pyramid_->bake(depth_att_id, "meshlet_depth_pyramid", "meshlet_depth_reduce_");

  draw_prep_->bake_task_commands(
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
          .final_depth_pyramid_tex = depth_pyramid_->texture(),
      },
      late_draws);

  {
    auto& p = frame.render_graph->add_graphics_pass("meshlet_occlusion_late");
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

    {
      RenderGraph* rg = frame.render_graph;
      rhi::Device* device = frame.device;
      ModelGPUMgr* model_gpu_mgr = frame.model_gpu_mgr;
      const glm::uvec2 out_ext = frame.output_extent;
      rhi::Swapchain* swapchain = frame.swapchain;
      p.set_ex([this, late_draws, depth_att_id, view_cb_suballoc, globals_cb_buf, gbuffer_a_id,
                gbuffer_b_id, meshlet_vis_rg_id, meshlet_stats_rg, meshlet_flags, cull_late_cb, rg,
                device, model_gpu_mgr, out_ext, swapchain](CmdEncoder* enc) {
        const glm::uvec2 vp_u = (out_ext.x > 0 && out_ext.y > 0)
                                    ? out_ext
                                    : glm::uvec2{swapchain->desc_.width, swapchain->desc_.height};
        const glm::ivec2 vp_dims{static_cast<int>(vp_u.x), static_cast<int>(vp_u.y)};
        enc->begin_rendering({
            RenderAttInfo::color_att(rg->get_att_img(gbuffer_a_id), LoadOp::Load,
                                     ClearValue{.color = glm::vec4{0.f}}),
            RenderAttInfo::color_att(rg->get_att_img(gbuffer_b_id), LoadOp::Load,
                                     ClearValue{.color = glm::vec4{0.f}}),
            RenderAttInfo::depth_stencil_att(
                rg->get_att_img(depth_att_id), LoadOp::Load,
                ClearValue{.depth_stencil = {.depth = reverse_z_ ? 0.f : 1.f, .stencil = 0}}),
        });

        auto& geo_batch = model_gpu_mgr->geometry_batch();
        encode_meshlet_test_draw_pass(
            reverse_z_, true, meshlet_flags, device, *rg, geo_batch,
            model_gpu_mgr->materials_allocator().get_buffer_handle(), globals_cb_buf,
            view_cb_suballoc, cull_late_cb, depth_pyramid_->texture(), vp_dims, meshlet_vis_rg_id,
            meshlet_stats_rg, late_draws.task_cmd_rg, rg->get_buf(late_draws.indirect_args_rg),
            model_gpu_mgr->instance_mgr(), std::span(meshlet_pso_late_), enc);

        enc->end_rendering();
      });
    }
  }

  add_buffer_readback_copy2(
      *frame.render_graph, "readback_task_cmd_group_count", early_draws.indirect_args_rg,
      frame.render_graph->import_external_buffer(
          task_cmd_group_count_readback_[frame.curr_frame_in_flight_idx].handle,
          "task_cmd_group_count_readback"),
      0, 0, sizeof(uint32_t));
  add_buffer_readback_copy2(
      *frame.render_graph, "readback_visible_object_count", late_draws.visible_object_count_rg,
      frame.render_graph->import_external_buffer(
          visible_object_count_readback_[frame.curr_frame_in_flight_idx].handle,
          "visible_object_count_readback"),
      0, 0, sizeof(uint32_t));

  add_buffer_readback_copy2(*frame.render_graph, "readback_meshlet_draw_stats", meshlet_stats_rg,
                            frame.render_graph->import_external_buffer(
                                meshlet_stats_buf_readback_[frame.curr_frame_in_flight_idx].handle,
                                "meshlet_draw_stats_readback"),
                            0, 0, k_meshlet_draw_stats_bytes);

  const bool depth_reduce_ran = final_depth_pyramid_rg.is_valid();

  {
    auto& p = frame.render_graph->add_graphics_pass("shade");
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

    p.w_swapchain_tex_new(frame.swapchain, frame.curr_swapchain_rg_id);

    {
      RenderGraph* rg = frame.render_graph;
      rhi::Device* device = frame.device;
      const glm::uvec2 out_ext = frame.output_extent;
      rhi::Swapchain* swapchain = frame.swapchain;
      p.set_ex([this, gbuffer_a_id, gbuffer_b_id, depth_att_id, shadow_output, depth_reduce_ran,
                globals_cb_buf, view_cb_suballoc, swapchain, device, rg, out_ext](CmdEncoder* enc) {
        enc->begin_rendering({
            RenderAttInfo::color_att(swapchain->get_current_texture(), LoadOp::DontCare),
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
        const glm::uvec2 dims = (out_ext.x > 0 && out_ext.y > 0)
                                    ? out_ext
                                    : glm::uvec2{swapchain->desc_.width, swapchain->desc_.height};
        enc->set_viewport({0, 0}, dims);
        enc->set_scissor({0, 0}, dims);

        const uint32_t gbuffer_a_bindless =
            device->get_tex(rg->get_att_img(gbuffer_a_id))->bindless_idx();
        const uint32_t gbuffer_b_bindless =
            device->get_tex(rg->get_att_img(gbuffer_b_id))->bindless_idx();
        const uint32_t depth_bindless =
            device->get_tex(rg->get_att_img(depth_att_id))->bindless_idx();
        uint32_t shadow_bindless = UINT32_MAX;
        if (shadow_output.valid) {
          shadow_bindless =
              device->get_tex(rg->get_att_img(shadow_output.depth_rg))->bindless_idx();
        }
        uint32_t pyramid_view_bindless = UINT32_MAX;
        glm::uvec2 pyramid_base{0, 0};
        if (depth_pyramid_->is_valid() && depth_reduce_ran) {
          pyramid_view_bindless = depth_pyramid_->debug_view_bindless_idx();
          pyramid_base = depth_pyramid_->dims();
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

        enc->end_rendering();
      });
    }
  }
}

}  // namespace teng::gfx
