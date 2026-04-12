#include "MeshletRendererTestScene.hpp"

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
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
#include "hlsl/shared_debug_meshlet_prepare.h"
#include "hlsl/shared_forward_meshlet.h"
#include "hlsl/shared_globals.h"
#include "hlsl/shared_meshlet_draw_stats.hlsli"
#include "hlsl/shared_task_cmd.h"
#include "imgui.h"

namespace teng::gfx {

using namespace rhi;

namespace {

[[maybe_unused]] constexpr const char* sponza_path = "Models/Sponza/glTF/Sponza.gltf";
[[maybe_unused]] constexpr const char* chessboard_path =
    "Models/ABeautifulGame/glTF_ktx2/ABeautifulGame.gltf";
[[maybe_unused]] constexpr const char* suzanne_path = "Models/Suzanne/glTF/Suzanne.gltf";
[[maybe_unused]] constexpr const char* cube_path = "Models/Cube/glTF/Cube.gltf";

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

std::filesystem::path resolve_model_path(const std::filesystem::path& resource_dir,
                                         const std::string& path) {
  if (path.starts_with("Models")) {
    return resource_dir / "models" / "gltf" / path;
  }
  return path;
}

uint32_t prev_pow2(uint32_t val) {
  uint32_t v = 1;
  while (v * 2 < val) {
    v *= 2;
  }
  return v;
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
    rhi::TextureHandle depth_pyramid_tex, glm::uvec2 viewport_dims, RGResourceId meshlet_vis_rg,
    RGResourceId meshlet_stats_rg, RGResourceId task_cmd_rg, rhi::BufferHandle indirect_buf,
    InstanceMgr& inst_mgr, std::span<const rhi::PipelineHandleHolder> psos, rhi::CmdEncoder* enc) {
  ASSERT(psos.size() == static_cast<size_t>(AlphaMaskType::Count));
  enc->set_wind_order(rhi::WindOrder::CounterClockwise);
  enc->set_cull_mode(rhi::CullMode::None);
  enc->set_viewport({0, 0}, viewport_dims);

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

}  // namespace

MeshletRendererScene::MeshletRendererScene(const TestSceneContext& ctx)
    : ITestScene(ctx), frame_uniform_gpu_allocator_(ctx_.device, true) {
  ASSERT(ctx_.model_gpu_mgr != nullptr);
  ASSERT(ctx_.shader_mgr != nullptr);
  ASSERT(ctx_.frame_staging != nullptr);

  test_model_handle_ = ResourceManager::get().load_model(
      resolve_model_path(ctx_.resource_dir, sponza_path), glm::mat4{1.f});
  ModelInstance* inst = ResourceManager::get().get_model(test_model_handle_);
  ASSERT(inst);
  auto alloc_opt = ctx_.model_gpu_mgr->instance_alloc(inst->instance_gpu_handle);
  ASSERT(alloc_opt.has_value());
  instance_alloc_ = *alloc_opt;
  const ModelGPUResources* res = ctx_.model_gpu_mgr->model_resources(inst->model_gpu_handle);
  ASSERT(res);
  ASSERT(res->totals.task_cmd_count == ctx_.model_gpu_mgr->geometry_batch().task_cmd_count);

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
  prepare_meshlets_pso_ = ctx_.shader_mgr->create_compute_pipeline(
      {.path = "debug_meshlet_prepare_meshlets", .type = rhi::ShaderType::Compute});
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

  fps_camera_.camera().pos = {0.f, 0.f, 3.f};
  fps_camera_.camera().pitch = 0.f;
  fps_camera_.camera().yaw = -90.f;
  fps_camera_.camera().calc_vectors();

  make_depth_pyramid_tex();
}

void MeshletRendererScene::ensure_meshlet_vis_buffer() {}

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
  if (meshlet_vis_buf_.handle.is_valid()) {
    ctx_.device->destroy(meshlet_vis_buf_.handle);
    meshlet_vis_buf_ = {};
  }
  ResourceManager::get().free_model(test_model_handle_);
}

void MeshletRendererScene::on_frame(const TestSceneContext& ctx) {
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
  ImGui::Checkbox("GPU object frustum cull", &gpu_object_frustum_cull_);
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
                   ImVec2(0, 1), ImVec2(1, 0));
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

CullData MeshletRendererScene::prepare_cull_data(const ViewData& vd) {
  const glm::mat4 projection_transpose = glm::transpose(vd.proj);
  auto normalize_plane = [](const glm::vec4& plane) {
    const glm::vec3 n = glm::vec3(plane);
    const float inv_len = 1.f / glm::length(n);
    return glm::vec4(n * inv_len, plane.w * inv_len);
  };
  const glm::vec4 frustum_x = normalize_plane(projection_transpose[0] + projection_transpose[3]);
  const glm::vec4 frustum_y = normalize_plane(projection_transpose[1] + projection_transpose[3]);

  CullData cd{};
  cd.frustum = glm::vec4(frustum_x.x, frustum_x.z, frustum_y.y, frustum_y.z);
  cd.z_near = 0.1f;
  cd.z_far = 100.f;
  cd.p00 = vd.proj[0][0];
  cd.p11 = vd.proj[1][1];
  cd.pyramid_width = 0;
  cd.pyramid_height = 0;
  cd.pyramid_mip_count = 0;
  cd.paused = 0;
  return cd;
}

CullData MeshletRendererScene::prepare_cull_data_late(const ViewData& vd) {
  CullData cd = prepare_cull_data(vd);
  if (depth_pyramid_tex_.is_valid()) {
    const auto* dp = ctx_.device->get_tex(depth_pyramid_tex_);
    cd.pyramid_width = dp->desc().dims.x;
    cd.pyramid_height = dp->desc().dims.y;
    cd.pyramid_mip_count = dp->desc().mip_levels;
  }
  return cd;
}

void MeshletRendererScene::add_render_graph_passes() {
  auto& batch = ctx_.model_gpu_mgr->geometry_batch();
  auto task_cmd_count = batch.task_cmd_count;
  if (task_cmd_count == 0 || batch.get_stats().vertex_count == 0) {
    return;
  }

  if (ctx_.swapchain) {
    make_depth_pyramid_tex();
  }
  ASSERT(depth_pyramid_tex_.is_valid());

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
    }
  }
  if (!meshlet_vis_buf_.handle.is_valid()) {
    return;
  }

  RGResourceId meshlet_vis_rg_id =
      ctx_.rg->import_external_buffer(meshlet_vis_buf_, "meshlet_test_meshlet_vis_rg");

  if (frame_num_ == 0) {
    const size_t need =
        ctx_.model_gpu_mgr->instance_mgr().get_num_meshlet_vis_buf_elements() * sizeof(uint32_t);
    auto& p = ctx_.rg->add_transfer_pass("meshlet_clear_meshlet_vis");
    meshlet_vis_rg_id = p.write_buf(meshlet_vis_rg_id, rhi::PipelineStage::AllTransfer);
    p.set_ex([need, this](rhi::CmdEncoder* enc) {
      enc->fill_buffer(meshlet_vis_buf_.handle, 0, need, 0);
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
  {
    GlobalData gd{};
    gd.render_mode = DEBUG_RENDER_MODE_NONE;
    gd.frame_num = 0;
    gd.meshlet_stats_enabled = 1;
    gd._padding = 0;
    globals_cb_buf = frame_uniform_gpu_allocator_.alloc2(sizeof(GlobalData), &gd);
  }

  RGResourceId task_cmd_dst_rg = ctx_.rg->create_buffer(
      {.size = task_cmd_count * sizeof(TaskCmd), .defer_reuse = true}, "meshlet_hello_task_cmds");
  constexpr size_t k_indirect_bytes =
      sizeof(uint32_t) * 3 * static_cast<size_t>(AlphaMaskType::Count);
  RGResourceId indirect_args_rg = ctx_.rg->create_buffer(
      {.size = k_indirect_bytes, .defer_reuse = true}, "meshlet_hello_indirect_args");
  RGResourceId visible_object_count_rg = ctx_.rg->create_buffer(
      {.size = sizeof(uint32_t), .defer_reuse = true}, "meshlet_visible_object_count");
  RGResourceId meshlet_stats_rg = ctx_.rg->create_buffer(
      {.size = k_meshlet_draw_stats_bytes, .defer_reuse = true}, "meshlet_test_draw_stats");

  const uint32_t max_draws = ctx_.model_gpu_mgr->instance_mgr().stats().max_instance_data_count;

  {
    auto& p = ctx_.rg->add_transfer_pass("meshlet_clear_visible_and_indirect");
    visible_object_count_rg = p.write_buf(visible_object_count_rg, rhi::PipelineStage::AllTransfer);
    indirect_args_rg = p.write_buf(indirect_args_rg, rhi::PipelineStage::AllTransfer);
    meshlet_stats_rg = p.write_buf(meshlet_stats_rg, rhi::PipelineStage::AllTransfer);
    p.set_ex([this, visible_object_count_rg, indirect_args_rg, meshlet_stats_rg](CmdEncoder* enc) {
      enc->fill_buffer(ctx_.rg->get_buf(visible_object_count_rg), 0, sizeof(uint32_t), 0);
      enc->fill_buffer(ctx_.rg->get_buf(indirect_args_rg), 0, k_indirect_bytes, 0);
      enc->fill_buffer(ctx_.rg->get_buf(meshlet_stats_rg), 0, k_meshlet_draw_stats_bytes, 0);
    });
  }

  {
    auto& p = ctx_.rg->add_compute_pass("meshlet_prepare_meshlets");
    task_cmd_dst_rg = p.write_buf(task_cmd_dst_rg, PipelineStage::ComputeShader);
    indirect_args_rg = p.rw_buf(indirect_args_rg, PipelineStage::ComputeShader);
    visible_object_count_rg = p.rw_buf(visible_object_count_rg, PipelineStage::ComputeShader);
    p.set_ex([this, task_cmd_dst_rg, indirect_args_rg, visible_object_count_rg, max_draws,
              view_cb_suballoc, cull_early_cb](CmdEncoder* enc) {
      enc->bind_pipeline(prepare_meshlets_pso_);
      DebugMeshletPreparePC pc{
          .dst_task_cmd_buf_idx =
              ctx_.device->get_buf(ctx_.rg->get_buf(task_cmd_dst_rg))->bindless_idx(),
          .draw_cnt_buf_idx =
              ctx_.device->get_buf(ctx_.rg->get_buf(indirect_args_rg))->bindless_idx(),
          .instance_data_buf_idx =
              ctx_.device->get_buf(ctx_.model_gpu_mgr->instance_mgr().get_instance_data_buf())
                  ->bindless_idx(),
          .mesh_data_buf_idx =
              ctx_.device
                  ->get_buf(ctx_.model_gpu_mgr->geometry_batch().mesh_buf.get_buffer_handle())
                  ->bindless_idx(),
          .max_draws = max_draws,
          .culling_enabled = gpu_object_frustum_cull_,
          .visible_obj_cnt_buf_idx =
              ctx_.device->get_buf(ctx_.rg->get_buf(visible_object_count_rg))->bindless_idx(),
      };

      enc->bind_cbv(view_cb_suballoc.buf, VIEW_DATA_SLOT, view_cb_suballoc.offset_bytes,
                    sizeof(ViewData));
      enc->bind_cbv(cull_early_cb.buf, 4, cull_early_cb.offset_bytes, sizeof(CullData));
      enc->push_constants(&pc, sizeof(pc));
      enc->dispatch_compute({align_divide_up(static_cast<uint64_t>(max_draws), 64ull), 1, 1},
                            {64, 1, 1});
    });
  }

  RGResourceId gbuffer_a_id =
      ctx_.rg->create_texture({.format = rhi::TextureFormat::R16G16B16A16Sfloat}, "gbuffer_a");
  RGResourceId depth_att = ctx_.rg->create_texture(
      {.format = TextureFormat::D32float, .size_class = SizeClass::Swapchain},
      "meshlet_hello_depth_att");
  RGResourceId depth_att_id{};

  uint32_t meshlet_flags = MESHLET_OCCLUSION_CULL_ENABLED_BIT;
  if (gpu_object_frustum_cull_) {
    meshlet_flags |= MESHLET_FRUSTUM_CULL_ENABLED_BIT;
  }

  {
    auto& p = ctx_.rg->add_graphics_pass("meshlet_occlusion_early");
    task_cmd_dst_rg =
        p.read_buf(task_cmd_dst_rg, PipelineStage::MeshShader | PipelineStage::TaskShader);
    indirect_args_rg =
        p.read_buf(indirect_args_rg, PipelineStage::TaskShader | PipelineStage::DrawIndirect,
                   AccessFlags::IndirectCommandRead);
    meshlet_vis_rg_id = p.write_buf(meshlet_vis_rg_id, PipelineStage::TaskShader);
    meshlet_stats_rg = p.rw_buf(meshlet_stats_rg, PipelineStage::TaskShader);
    gbuffer_a_id = p.write_color_output(gbuffer_a_id);
    depth_att_id = p.write_depth_output(depth_att);

    p.set_ex([this, task_cmd_dst_rg, indirect_args_rg, depth_att_id, view_cb_suballoc,
              globals_cb_buf, gbuffer_a_id, meshlet_vis_rg_id, meshlet_stats_rg, meshlet_flags,
              cull_early_cb](CmdEncoder* enc) {
      glm::vec4 clear_color{0.06f, 0.07f, 0.09f, 1.f};
      const glm::uvec2 vp_dims{ctx_.swapchain->desc_.width, ctx_.swapchain->desc_.height};
      ctx_.device->enqueue_swapchain_for_present(ctx_.swapchain, enc);
      enc->begin_rendering({
          RenderAttInfo::color_att(ctx_.rg->get_att_img(gbuffer_a_id), LoadOp::Clear,
                                   ClearValue{.color = clear_color}),
          RenderAttInfo::depth_stencil_att(
              ctx_.rg->get_att_img(depth_att_id), LoadOp::Clear,
              ClearValue{.depth_stencil = {.depth = reverse_z_ ? 0.f : 1.f, .stencil = 0}}),
      });

      auto& geo_batch = ctx_.model_gpu_mgr->geometry_batch();
      encode_meshlet_test_draw_pass(
          reverse_z_, false, meshlet_flags, ctx_.device, *ctx_.rg, geo_batch,
          ctx_.model_gpu_mgr->materials_allocator().get_buffer_handle(), globals_cb_buf,
          view_cb_suballoc, cull_early_cb, rhi::TextureHandle{}, vp_dims, meshlet_vis_rg_id,
          meshlet_stats_rg, task_cmd_dst_rg, ctx_.rg->get_buf(indirect_args_rg),
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

  {
    auto& p = ctx_.rg->add_graphics_pass("meshlet_occlusion_late");
    task_cmd_dst_rg =
        p.read_buf(task_cmd_dst_rg, PipelineStage::MeshShader | PipelineStage::TaskShader);
    indirect_args_rg =
        p.read_buf(indirect_args_rg, PipelineStage::TaskShader | PipelineStage::DrawIndirect,
                   AccessFlags::IndirectCommandRead);
    if (final_depth_pyramid_rg.is_valid()) {
      p.sample_tex(final_depth_pyramid_rg, rhi::PipelineStage::TaskShader,
                   RgSubresourceRange::all_mips_all_slices());
    }
    meshlet_vis_rg_id = p.rw_buf(meshlet_vis_rg_id, PipelineStage::TaskShader);
    meshlet_stats_rg = p.rw_buf(meshlet_stats_rg, PipelineStage::TaskShader);
    visible_object_count_rg = p.copy_from_buf(visible_object_count_rg);
    gbuffer_a_id = p.rw_color_output(gbuffer_a_id);
    depth_att_id = p.rw_depth_output(depth_att);

    p.set_ex([this, task_cmd_dst_rg, indirect_args_rg, depth_att_id, view_cb_suballoc,
              globals_cb_buf, gbuffer_a_id, meshlet_vis_rg_id, meshlet_stats_rg, meshlet_flags,
              cull_late_cb](CmdEncoder* enc) {
      const glm::uvec2 vp_dims{ctx_.swapchain->desc_.width, ctx_.swapchain->desc_.height};
      enc->begin_rendering({
          RenderAttInfo::color_att(ctx_.rg->get_att_img(gbuffer_a_id), LoadOp::Load,
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
          meshlet_stats_rg, task_cmd_dst_rg, ctx_.rg->get_buf(indirect_args_rg),
          ctx_.model_gpu_mgr->instance_mgr(), std::span(meshlet_pso_late_), enc);

      enc->end_rendering();
    });
  }

  add_buffer_readback_copy2(
      *ctx_.rg, "readback_task_cmd_group_count", indirect_args_rg,
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
    if (depth_reduce_ran) {
      p.sample_tex(final_depth_pyramid_rg, rhi::PipelineStage::FragmentShader,
                   RgSubresourceRange::all_mips_all_slices());
    }
    p.w_swapchain_tex(ctx_.swapchain);
    p.set_ex([this, gbuffer_a_id, depth_reduce_ran](CmdEncoder* enc) {
      enc->begin_rendering({
          RenderAttInfo::color_att(ctx_.swapchain->get_current_texture(), LoadOp::DontCare),
      });
      enc->bind_pipeline(shade_pso_);
      enc->set_wind_order(rhi::WindOrder::CounterClockwise);
      enc->set_cull_mode(rhi::CullMode::None);
      glm::uvec2 dims = {ctx_.swapchain->desc_.width, ctx_.swapchain->desc_.height};
      enc->set_viewport({0, 0}, dims);
      enc->set_scissor({0, 0}, dims);

      const uint32_t gbuffer_bindless =
          ctx_.device->get_tex(ctx_.rg->get_att_img(gbuffer_a_id))->bindless_idx();
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
          .gbuffer_a_idx = gbuffer_bindless,
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
