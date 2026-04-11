#include "MeshletRendererTestScene.hpp"

#include "ResourceManager.hpp"
#include "Window.hpp"
#include "core/Logger.hpp"
#include "core/Util.hpp"
#include "gfx/ImGuiRenderer.hpp"
#include "gfx/ModelGPUManager.hpp"
#include "gfx/ShaderManager.hpp"
#include "gfx/rhi/Swapchain.hpp"
#include "gfx/rhi/Texture.hpp"
#include "hlsl/shader_constants.h"
#include "hlsl/shared_debug_meshlet_prepare.h"
#include "hlsl/shared_forward_meshlet.h"
#include "hlsl/shared_task_cmd.h"
#include "hlsl/shared_test_clear_buf.h"
#include "imgui.h"

namespace teng::gfx {

using namespace rhi;

namespace {

[[maybe_unused]] constexpr const char* sponza_path = "Models/Sponza/glTF/Sponza.gltf";
[[maybe_unused]] constexpr const char* chessboard_path =
    "Models/ABeautifulGame/glTF_ktx2/ABeautifulGame.gltf";
[[maybe_unused]] constexpr const char* suzanne_path = "Models/Suzanne/glTF/Suzanne.gltf";
[[maybe_unused]] constexpr const char* cube_path = "Models/Cube/glTF/Cube.gltf";

std::filesystem::path resolve_model_path(const std::filesystem::path& resource_dir,
                                         const std::string& path) {
  if (path.starts_with("Models")) {
    return resource_dir / "models" / "gltf" / path;
  }
  return path;
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

  shade_pso_ = ctx_.shader_mgr->create_graphics_pipeline({
      .shaders = {{{"fullscreen_quad", ShaderType::Vertex},
                   {"meshlet_test/shade", ShaderType::Fragment}}},
      .name = "meshlet_test/shade",
  });
  clear_indirect_pso_ = ctx_.shader_mgr->create_compute_pipeline(
      {.path = "test_clear_cnt_buf", .type = rhi::ShaderType::Compute});
  prepare_meshlets_pso_ = ctx_.shader_mgr->create_compute_pipeline(
      {.path = "debug_meshlet_prepare_meshlets", .type = rhi::ShaderType::Compute});

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
  }

  fps_camera_.camera().pos = {0.f, 0.f, 3.f};
  fps_camera_.camera().pitch = 0.f;
  fps_camera_.camera().yaw = -90.f;
  fps_camera_.camera().calc_vectors();

  recreate_meshlet_pso();
}

void MeshletRendererScene::shutdown() {
  if (ctx_.window) {
    fps_camera_.set_mouse_captured(ctx_.window->get_handle(), false);
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
  if (frame_num_ >= ctx_.device->get_info().frames_in_flight) {
    const uint32_t curr = ctx_.curr_frame_in_flight_idx;
    visible_meshlet_task_groups = *reinterpret_cast<const uint32_t*>(
        ctx_.device->get_buf(task_cmd_group_count_readback_[curr].handle)->contents());
    visible_objects = *reinterpret_cast<const uint32_t*>(
        ctx_.device->get_buf(visible_object_count_readback_[curr].handle)->contents());
  }
  ImGui::Text("Visible mesh task groups (GPU): %u", visible_meshlet_task_groups);
  ImGui::Text("Visible objects (GPU): %u", visible_objects);
  ImGui::End();
}

ViewData MeshletRendererScene::prepare_view_data() {
  const float aspect = static_cast<float>(ctx_.swapchain->desc_.width) /
                       std::max(1.f, static_cast<float>(ctx_.swapchain->desc_.height));
  glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(60.f), aspect, 0.1f, 100.f);
  proj[1][1] = -proj[1][1];
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

void MeshletRendererScene::add_render_graph_passes() {
  auto& batch = ctx_.model_gpu_mgr->geometry_batch();
  auto task_cmd_count = batch.task_cmd_count;
  if (task_cmd_count == 0 || batch.get_stats().vertex_count == 0) {
    return;
  }

  frame_uniform_gpu_allocator_.set_frame_idx(ctx_.curr_frame_in_flight_idx);

  frame_num_++;

  auto vd = prepare_view_data();
  auto view_cb_suballoc = frame_uniform_gpu_allocator_.alloc2(sizeof(ViewData), &vd);

  auto cd = prepare_cull_data(vd);
  auto cull_storage_cb_buf = frame_uniform_gpu_allocator_.alloc2(sizeof(CullData), &cd);

  BufferSuballoc globals_cb_buf;
  {
    GlobalData gd{};
    gd.render_mode = DEBUG_RENDER_MODE_NONE;
    gd.frame_num = 0;
    gd.meshlet_stats_enabled = 0;
    gd._padding = 0;
    globals_cb_buf = frame_uniform_gpu_allocator_.alloc2(sizeof(GlobalData), &gd);
  }

  RGResourceId task_cmd_dst_rg = ctx_.rg->create_buffer(
      {.size = task_cmd_count * sizeof(TaskCmd), .defer_reuse = true}, "meshlet_hello_task_cmds");
  RGResourceId indirect_args_rg = ctx_.rg->create_buffer(
      {.size = sizeof(uint32_t) * 3, .defer_reuse = true}, "meshlet_hello_indirect_args");
  RGResourceId visible_object_count_rg = ctx_.rg->create_buffer(
      {.size = sizeof(uint32_t), .defer_reuse = true}, "meshlet_visible_object_count");

  const uint32_t max_draws = ctx_.model_gpu_mgr->instance_mgr().stats().max_instance_data_count;

  {
    auto& p = ctx_.rg->add_transfer_pass("meshlet_clear_visible_obj_count");
    visible_object_count_rg = p.write_buf(visible_object_count_rg, rhi::PipelineStage::AllTransfer);
    p.set_ex([this, visible_object_count_rg](CmdEncoder* enc) {
      enc->fill_buffer(ctx_.rg->get_buf(visible_object_count_rg), 0, sizeof(uint32_t), 0);
    });
  }

  {
    auto& p = ctx_.rg->add_compute_pass("meshlet_clear_indirect");
    indirect_args_rg = p.write_buf(indirect_args_rg, PipelineStage::ComputeShader);
    p.set_ex([this, indirect_args_rg](CmdEncoder* enc) {
      enc->bind_pipeline(clear_indirect_pso_);
      TestClearBufPC pc{
          .buf_idx = ctx_.device->get_buf(ctx_.rg->get_buf(indirect_args_rg))->bindless_idx()};
      enc->push_constants(&pc, sizeof(pc));
      enc->dispatch_compute({1, 1, 1}, {1, 1, 1});
    });
  }

  {
    auto& p = ctx_.rg->add_compute_pass("meshlet_prepare_meshlets");
    task_cmd_dst_rg = p.write_buf(task_cmd_dst_rg, PipelineStage::ComputeShader);
    indirect_args_rg = p.rw_buf(indirect_args_rg, PipelineStage::ComputeShader);
    visible_object_count_rg = p.rw_buf(visible_object_count_rg, PipelineStage::ComputeShader);
    p.set_ex([this, task_cmd_dst_rg, indirect_args_rg, visible_object_count_rg, max_draws,
              view_cb_suballoc, cull_storage_cb_buf](CmdEncoder* enc) {
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
      enc->bind_cbv(cull_storage_cb_buf.buf, 4, cull_storage_cb_buf.offset_bytes, sizeof(CullData));
      enc->push_constants(&pc, sizeof(pc));
      enc->dispatch_compute({align_divide_up(static_cast<uint64_t>(max_draws), 64ull), 1, 1},
                            {64, 1, 1});
    });
  }

  RGResourceId gbuffer_a_id =
      ctx_.rg->create_texture({.format = rhi::TextureFormat::R16G16B16A16Sfloat}, "gbuffer_a");

  {
    auto& p = ctx_.rg->add_graphics_pass("meshlet_hello");
    task_cmd_dst_rg =
        p.read_buf(task_cmd_dst_rg, PipelineStage::MeshShader | PipelineStage::TaskShader);
    indirect_args_rg =
        p.read_buf(indirect_args_rg, PipelineStage::TaskShader | PipelineStage::DrawIndirect,
                   AccessFlags::IndirectCommandRead);
    visible_object_count_rg = p.copy_from_buf(visible_object_count_rg);
    gbuffer_a_id = p.write_color_output(gbuffer_a_id);
    auto depth_att = ctx_.rg->create_texture(
        {.format = TextureFormat::D32float, .size_class = SizeClass::Swapchain},
        "meshlet_hello_depth_att");

    auto depth_att_id = p.write_depth_output(depth_att);
    p.set_ex([this, task_cmd_dst_rg, indirect_args_rg, depth_att_id, view_cb_suballoc,
              globals_cb_buf, gbuffer_a_id](CmdEncoder* enc) {
      glm::vec4 clear_color{0.06f, 0.07f, 0.09f, 1.f};
      ctx_.device->enqueue_swapchain_for_present(ctx_.swapchain, enc);
      enc->begin_rendering({
          RenderAttInfo::color_att(ctx_.rg->get_att_img(gbuffer_a_id), LoadOp::Clear,
                                   ClearValue{.color = clear_color}),
          RenderAttInfo::depth_stencil_att(
              ctx_.rg->get_att_img(depth_att_id), LoadOp::Clear,
              ClearValue{.depth_stencil = {.depth = 1.f, .stencil = 0}}),
      });
      enc->bind_pipeline(meshlet_pso_);
      enc->set_cull_mode(CullMode::Back);
      enc->set_wind_order(WindOrder::CounterClockwise);
      enc->set_viewport({0, 0}, {ctx_.swapchain->desc_.width, ctx_.swapchain->desc_.height});
      enc->set_scissor({0, 0}, {ctx_.swapchain->desc_.width, ctx_.swapchain->desc_.height});

      auto& geo_batch = ctx_.model_gpu_mgr->geometry_batch();
      enc->bind_srv(geo_batch.mesh_buf.get_buffer_handle(), 5);
      enc->bind_srv(geo_batch.meshlet_buf.get_buffer_handle(), 6);
      enc->bind_srv(geo_batch.meshlet_triangles_buf.get_buffer_handle(), 7);
      enc->bind_srv(geo_batch.meshlet_vertices_buf.get_buffer_handle(), 8);
      enc->bind_srv(geo_batch.vertex_buf.get_buffer_handle(), 9);
      enc->bind_srv(ctx_.model_gpu_mgr->instance_mgr().get_instance_data_buf(), 10);
      enc->bind_srv(ctx_.rg->get_buf(task_cmd_dst_rg), 4);
      enc->bind_srv(ctx_.model_gpu_mgr->materials_allocator().get_buffer_handle(), 11);

      enc->bind_cbv(globals_cb_buf.buf, GLOBALS_SLOT, globals_cb_buf.offset_bytes,
                    sizeof(GlobalData));
      enc->bind_cbv(view_cb_suballoc.buf, VIEW_DATA_SLOT, view_cb_suballoc.offset_bytes,
                    sizeof(ViewData));

      Task2PC task_pc{
          .flags = 0,
          .alpha_test_enabled = 0,
          .out_draw_count_buf_idx =
              ctx_.device->get_buf(ctx_.rg->get_buf(indirect_args_rg))->bindless_idx(),
      };
      enc->push_constants(&task_pc, sizeof(task_pc));

      enc->draw_mesh_threadgroups_indirect(ctx_.rg->get_buf(indirect_args_rg), 0,
                                           {K_TASK_TG_SIZE, 1, 1}, {K_MESH_TG_SIZE, 1, 1});

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

  {
    auto& p = ctx_.rg->add_graphics_pass("shade");
    gbuffer_a_id = p.sample_tex(gbuffer_a_id);
    p.w_swapchain_tex(ctx_.swapchain);
    p.set_ex([this, gbuffer_a_id](CmdEncoder* enc) {
      enc->begin_rendering({
          RenderAttInfo::color_att(ctx_.swapchain->get_current_texture(), LoadOp::DontCare),
      });
      enc->bind_pipeline(shade_pso_);
      enc->bind_srv(ctx_.rg->get_att_img(gbuffer_a_id), 0);
      enc->set_wind_order(rhi::WindOrder::CounterClockwise);
      enc->set_cull_mode(rhi::CullMode::None);
      glm::uvec2 dims = {ctx_.swapchain->desc_.width, ctx_.swapchain->desc_.height};
      enc->set_viewport({0, 0}, dims);
      enc->set_scissor({0, 0}, dims);
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

void MeshletRendererScene::recreate_meshlet_pso() {
  meshlet_pso_ = ctx_.shader_mgr->create_graphics_pipeline({
      .shaders = {{"debug_meshlet_hello", ShaderType::Task},
                  {"debug_meshlet_hello", ShaderType::Mesh},
                  {"debug_meshlet_hello", ShaderType::Fragment}},
      .depth_stencil = GraphicsPipelineCreateInfo::depth_enable(true, CompareOp::Less),
      .name = "debug_meshlet_hello",
  });
}

}  // namespace teng::gfx