#include "GBufferRenderer.hpp"

#include <string>

#include "gfx/ShaderManager.hpp"
#include "gfx/renderer/Context.hpp"
#include "gfx/renderer/InstanceMgr.hpp"
#include "gfx/renderer/TaskCmdBufRgIds.hpp"
#include "hlsl/shader_constants.h"
#include "hlsl/shared_forward_meshlet.h"
#include "hlsl/shared_globals.h"

namespace TENG_NAMESPACE {

using namespace rhi;

namespace gfx {

namespace {

void encode_meshlet_mesh_draw_pass(
    rhi::Device* device, RenderGraph& rg, InstanceMgr& static_instance_mgr, rhi::CmdEncoder* enc,
    DrawCullPhase cull_phase, bool enable_meshlet_occlusion_cull, rhi::TextureHandle depth_handle,
    const GBufferRenderer::SceneBindings& scene,
    const GBufferRenderer::MeshletMeshPassView& mesh_pass,
    std::span<const rhi::PipelineHandleHolder> meshlet_psos_by_alpha_mask) {
  enc->set_depth_stencil_state(
      get_ctx().settings.reverse_z ? rhi::CompareOp::Greater : rhi::CompareOp::Less, true);
  enc->set_wind_order(rhi::WindOrder::CounterClockwise);
  enc->set_cull_mode(rhi::CullMode::Back);
  enc->set_viewport({0, 0}, device->get_tex(depth_handle)->desc().dims);

  ASSERT(get_ctx().settings.mesh_shaders_enabled);
  ASSERT(meshlet_psos_by_alpha_mask.size() == static_cast<size_t>(AlphaMaskType::Count));

  enc->bind_uav(rg.get_buf(mesh_pass.meshlet_vis), 1);
  enc->bind_uav(rg.get_buf(mesh_pass.meshlet_draw_stats), 2);
  if (cull_phase == DrawCullPhase::Late) {
    enc->bind_srv(mesh_pass.render_view.depth_pyramid_tex.handle, 3);
  }
  enc->bind_srv(scene.draw_batch.mesh_buf.get_buffer_handle(), 5);
  enc->bind_srv(scene.draw_batch.meshlet_buf.get_buffer_handle(), 6);
  enc->bind_srv(scene.draw_batch.meshlet_triangles_buf.get_buffer_handle(), 7);
  enc->bind_srv(scene.draw_batch.meshlet_vertices_buf.get_buffer_handle(), 8);
  enc->bind_srv(scene.draw_batch.vertex_buf.get_buffer_handle(), 9);
  enc->bind_srv(static_instance_mgr.get_instance_data_buf(), 10);
  enc->bind_cbv(scene.frame_globals_buf_info.buf, GLOBALS_SLOT,
                scene.frame_globals_buf_info.offset_bytes);
  enc->bind_cbv(mesh_pass.render_view.data_buf_info.buf, VIEW_DATA_SLOT,
                mesh_pass.render_view.data_buf_info.offset_bytes);
  enc->bind_cbv(mesh_pass.render_view.cull_data_buf_info.buf, 4,
                mesh_pass.render_view.cull_data_buf_info.offset_bytes);
  Task2PC pc{
      .pass = static_cast<uint32_t>(cull_phase),
      .flags = 0,
      .out_draw_count_buf_idx = device->get_buf(mesh_pass.out_draw_count_buf)->bindless_idx(),
  };
  if (get_ctx().settings.meshlet_frustum_culling_enabled && get_ctx().settings.culling_enabled) {
    pc.flags |= MESHLET_FRUSTUM_CULL_ENABLED_BIT;
  }
  if (get_ctx().settings.meshlet_cone_culling_enabled && get_ctx().settings.culling_enabled) {
    pc.flags |= MESHLET_CONE_CULL_ENABLED_BIT;
  }
  if (enable_meshlet_occlusion_cull && get_ctx().settings.meshlet_occlusion_culling_enabled &&
      get_ctx().settings.culling_enabled) {
    pc.flags |= MESHLET_OCCLUSION_CULL_ENABLED_BIT;
  }

  for (size_t alpha_mask_type = 0; alpha_mask_type < AlphaMaskType::Count; alpha_mask_type++) {
    auto task_cmd_buf_handle =
        rg.get_buf(mesh_pass.task_cmd_buf_rg_ids[static_cast<AlphaMaskType>(alpha_mask_type)]);
    enc->bind_srv(task_cmd_buf_handle, 4);
    enc->bind_pipeline(meshlet_psos_by_alpha_mask[alpha_mask_type]);
    pc.alpha_test_enabled = static_cast<uint32_t>(alpha_mask_type);
    enc->push_constants(&pc, sizeof(pc));
    enc->draw_mesh_threadgroups_indirect(mesh_pass.out_draw_count_buf,
                                         alpha_mask_type * sizeof(uint32_t) * 3,
                                         {K_TASK_TG_SIZE, 1, 1}, {K_MESH_TG_SIZE, 1, 1});
  }
}

}  // namespace

// draw_batch / render_view pointers must stay valid until this pass's ExecuteFn runs.
GBufferRenderer::GBufferRenderer(rhi::Device* device, InstanceMgr& static_instance_mgr,
                                 RenderGraph& rg)
    : device_(device), static_instance_mgr_(static_instance_mgr), rg_(rg) {}

GBufferRenderer::~GBufferRenderer() = default;

void GBufferRenderer::bake(GbufferPassInfo& gbuffer_pass_info, DrawCullPhase cull_phase,
                           const SceneBindings& scene, const GBufferViewBindings& view) {
  bool late = cull_phase == DrawCullPhase::Late;
  auto& p = rg_.add_graphics_pass(late ? "gbuffer_late" : "gbuffer_early");
  RGResourceId out_draw_count_buf_rg_handle{};

  if (get_ctx().settings.mesh_shaders_enabled) {
    for (size_t alpha_mask_type = 0; alpha_mask_type < AlphaMaskType::Count; alpha_mask_type++) {
      if (scene.draw_batch.get_stats().vertex_count > 0) {
        auto id =
            view.task_cmd_buf_rg_ids.phase(cull_phase)[static_cast<AlphaMaskType>(alpha_mask_type)];
        ASSERT(id.is_valid());
        p.read_buf(id, PipelineStage::MeshShader | PipelineStage::TaskShader);
      }
    }
    out_draw_count_buf_rg_handle = p.read_buf(view.rg_ids.draw_count, PipelineStage::TaskShader);
    if (late) {
      p.sample_tex(view.rg_ids.final_depth_pyramid, PipelineStage::TaskShader);
      view.rg_ids.meshlet_vis = p.rw_buf(view.rg_ids.meshlet_vis, PipelineStage::TaskShader);
    } else {
      ASSERT(static_instance_mgr_.get_num_meshlet_vis_buf_elements() > 0);
      view.rg_ids.meshlet_vis = rg_.create_buffer(
          {.size = sizeof(uint32_t) * static_instance_mgr_.get_num_meshlet_vis_buf_elements(),
           .defer_reuse = true},
          "meshlet_vis_buf");
      p.write_buf(view.rg_ids.meshlet_vis, PipelineStage::TaskShader);
    }
  } else {
    // TODO: handle indirect case
    // p.read_buf(indirect_buffer_id, PipelineStage::DrawIndirect);
  }

  if (late) {
    gbuffer_pass_info.gbuffer_a_id = p.rw_color_output(gbuffer_pass_info.gbuffer_a_id);
    gbuffer_pass_info.gbuffer_b_id = p.rw_color_output(gbuffer_pass_info.gbuffer_b_id);
    gbuffer_pass_info.depth_id = p.rw_depth_output(gbuffer_pass_info.depth_id);
  } else {
    gbuffer_pass_info.gbuffer_a_id =
        rg_.create_texture({.format = rhi::TextureFormat::R16G16B16A16Sfloat}, "gbuffer_a");
    gbuffer_pass_info.gbuffer_b_id =
        rg_.create_texture({.format = rhi::TextureFormat::R8G8B8A8Unorm}, "gbuffer_b");
    p.write_color_output(gbuffer_pass_info.gbuffer_a_id);
    p.write_color_output(gbuffer_pass_info.gbuffer_b_id);
    gbuffer_pass_info.depth_id =
        rg_.create_texture({.format = rhi::TextureFormat::D32float}, "depth_tex");
    p.write_depth_output(gbuffer_pass_info.depth_id);
  }
  RGResourceId meshlet_stats_for_pass{};
  if (get_ctx().settings.mesh_shaders_enabled) {
    view.rg_ids.meshlet_draw_stats =
        p.rw_buf(view.rg_ids.meshlet_draw_stats, rhi::PipelineStage::TaskShader);
    meshlet_stats_for_pass = view.rg_ids.meshlet_draw_stats;
  }

  const RGResourceId meshlet_vis_for_pass = view.rg_ids.meshlet_vis;
  p.set_ex([this, late, cull_phase, rg_a = gbuffer_pass_info.gbuffer_a_id,
            rg_b = gbuffer_pass_info.gbuffer_b_id, rg_depth = gbuffer_pass_info.depth_id,
            rv = &view.render_view, meshlet_vis_rg = meshlet_vis_for_pass,
            meshlet_stats_rg = meshlet_stats_for_pass, batch = &scene.draw_batch,
            materials = scene.materials_buf, frame_globals = scene.frame_globals_buf_info,
            out_draw_count_rg = out_draw_count_buf_rg_handle,
            task_cmd_rg = view.task_cmd_buf_rg_ids.phase(cull_phase)](rhi::CmdEncoder* enc) {
    if (!static_instance_mgr_.has_draws()) {
      return;
    }
    auto depth_handle = rg_.get_att_img(rg_depth);
    ASSERT(depth_handle.is_valid());
    auto gbuffer_a_tex = rg_.get_att_img(rg_a);
    auto gbuffer_b_tex = rg_.get_att_img(rg_b);
    auto load_op = late ? rhi::LoadOp::Load : rhi::LoadOp::Clear;
    enc->begin_rendering({
        RenderAttInfo::color_att(gbuffer_a_tex, load_op, {.color = glm::vec4(0, 0, 0, 0)}),
        RenderAttInfo::color_att(gbuffer_b_tex, load_op, {.color = glm::vec4(0, 0, 0, 0)}),
        RenderAttInfo::depth_stencil_att(
            depth_handle, load_op,
            {.depth_stencil = {.depth = get_ctx().settings.reverse_z ? 0.f : 1.f}}),
    });
    enc->bind_srv(materials, 11);

    if (get_ctx().settings.mesh_shaders_enabled) {
      const SceneBindings mesh_scene{*batch, materials, frame_globals};
      const MeshletMeshPassView mesh_pass{*rv, meshlet_vis_rg, meshlet_stats_rg, task_cmd_rg,
                                          rg_.get_buf(out_draw_count_rg)};
      encode_meshlet_mesh_draw_pass(
          device_, rg_, static_instance_mgr_, enc, cull_phase, true, depth_handle, mesh_scene,
          mesh_pass,
          std::span<const rhi::PipelineHandleHolder>(gbuffer_meshlet_psos_,
                                                     static_cast<size_t>(AlphaMaskType::Count)));
    } else {
      ASSERT(0 && "Not implemented");
    }
    enc->end_rendering();
  });
}

void GBufferRenderer::bake_shadow_depth(std::string_view pass_name, ShadowDepthPassInfo& out,
                                        DrawCullPhase cull_phase, const SceneBindings& scene,
                                        const GBufferViewBindings& view) {
  bool late = cull_phase == DrawCullPhase::Late;
  auto& p = rg_.add_graphics_pass(std::string(pass_name));
  RGResourceId out_draw_count_buf_rg_handle{};

  ASSERT(get_ctx().settings.mesh_shaders_enabled);
  for (size_t alpha_mask_type = 0; alpha_mask_type < AlphaMaskType::Count; alpha_mask_type++) {
    if (scene.draw_batch.get_stats().vertex_count > 0) {
      auto id =
          view.task_cmd_buf_rg_ids.phase(cull_phase)[static_cast<AlphaMaskType>(alpha_mask_type)];
      ASSERT(id.is_valid());
      p.read_buf(id, PipelineStage::MeshShader | PipelineStage::TaskShader);
    }
  }
  out_draw_count_buf_rg_handle = p.read_buf(view.rg_ids.draw_count, PipelineStage::TaskShader);
  if (late) {
    p.sample_tex(view.rg_ids.final_depth_pyramid, PipelineStage::TaskShader);
    view.rg_ids.meshlet_vis = p.rw_buf(view.rg_ids.meshlet_vis, PipelineStage::TaskShader);
  } else {
    ASSERT(static_instance_mgr_.get_num_meshlet_vis_buf_elements() > 0);
    view.rg_ids.meshlet_vis = rg_.create_buffer(
        {.size = sizeof(uint32_t) * static_instance_mgr_.get_num_meshlet_vis_buf_elements(),
         .defer_reuse = true},
        std::string("meshlet_vis_buf_shadow_").append(pass_name));
    p.write_buf(view.rg_ids.meshlet_vis, PipelineStage::TaskShader);
  }

  if (late) {
    out.depth_id = p.rw_depth_output(out.depth_id);
  } else {
    out.depth_id = rg_.create_texture({.format = rhi::TextureFormat::D32float}, "shadow_depth_tex");
    p.write_depth_output(out.depth_id);
  }

  view.rg_ids.meshlet_draw_stats =
      p.rw_buf(view.rg_ids.meshlet_draw_stats, rhi::PipelineStage::TaskShader);
  const RGResourceId meshlet_stats_for_pass = view.rg_ids.meshlet_draw_stats;
  const RGResourceId meshlet_vis_for_pass = view.rg_ids.meshlet_vis;

  p.set_ex([this, late, cull_phase, rg_depth = out.depth_id, rv = &view.render_view,
            meshlet_vis_rg = meshlet_vis_for_pass, meshlet_stats_rg = meshlet_stats_for_pass,
            batch = &scene.draw_batch, materials = scene.materials_buf,
            frame_globals = scene.frame_globals_buf_info,
            out_draw_count_rg = out_draw_count_buf_rg_handle,
            task_cmd_rg = view.task_cmd_buf_rg_ids.phase(cull_phase)](rhi::CmdEncoder* enc) {
    if (!static_instance_mgr_.has_draws()) {
      return;
    }
    auto depth_handle = rg_.get_att_img(rg_depth);
    ASSERT(depth_handle.is_valid());
    auto load_op = late ? rhi::LoadOp::Load : rhi::LoadOp::Clear;
    enc->begin_rendering({
        RenderAttInfo::depth_stencil_att(
            depth_handle, load_op,
            {.depth_stencil = {.depth = get_ctx().settings.reverse_z ? 0.f : 1.f}}),
    });
    enc->bind_srv(materials, 11);

    const SceneBindings mesh_scene{*batch, materials, frame_globals};
    const MeshletMeshPassView mesh_pass{*rv, meshlet_vis_rg, meshlet_stats_rg, task_cmd_rg,
                                        rg_.get_buf(out_draw_count_rg)};
    encode_meshlet_mesh_draw_pass(
        device_, rg_, static_instance_mgr_, enc, cull_phase, false, depth_handle, mesh_scene,
        mesh_pass,
        std::span<const rhi::PipelineHandleHolder>(shadow_meshlet_psos_,
                                                   static_cast<size_t>(AlphaMaskType::Count)));
    enc->end_rendering();
  });
}

void GBufferRenderer::load_pipelines(ShaderManager& shader_mgr) {
  test2_pso_ = shader_mgr.create_graphics_pipeline({
      .shaders = {{{"basic_indirect", ShaderType::Vertex},
                   {"basic_indirect", ShaderType::Fragment}}},
  });

  for (size_t alpha_mask_type = 0; alpha_mask_type < AlphaMaskType::Count; alpha_mask_type++) {
    gbuffer_meshlet_psos_[alpha_mask_type] = shader_mgr.create_graphics_pipeline({
        .shaders = {{{"forward_meshlet", ShaderType::Task},
                     {"gbuffer_meshlet", ShaderType::Mesh},
                     {alpha_mask_type == AlphaMaskType::Mask ? "forward_meshlet_alphatest"
                                                             : "forward_meshlet",
                      ShaderType::Fragment}}},
    });
    shadow_meshlet_psos_[alpha_mask_type] = shader_mgr.create_graphics_pipeline({
        .shaders = {{{"forward_meshlet", ShaderType::Task},
                     {"gbuffer_meshlet", ShaderType::Mesh},
                     {alpha_mask_type == AlphaMaskType::Mask ? "shadow_depth_meshlet_alphatest"
                                                             : "shadow_depth_meshlet",
                      ShaderType::Fragment}}},
        .rendering = {.depth_format = rhi::TextureFormat::D32float},
        .depth_stencil = rhi::GraphicsPipelineCreateInfo::depth_enable(true, rhi::CompareOp::Less),
    });
  }
}

}  // namespace gfx

}  // namespace TENG_NAMESPACE
