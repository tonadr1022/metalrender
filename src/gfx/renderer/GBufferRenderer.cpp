#include "GBufferRenderer.hpp"

#include "gfx/ShaderManager.hpp"
#include "gfx/renderer/Context.hpp"
#include "gfx/renderer/InstanceMgr.hpp"
#include "gfx/renderer/TaskCmdBufRgIds.hpp"
#include "gfx/rhi/Texture.hpp"
#include "hlsl/shader_constants.h"
#include "hlsl/shared_forward_meshlet.h"
#include "hlsl/shared_globals.h"

namespace TENG_NAMESPACE {

using namespace rhi;

namespace gfx {

GBufferRenderer::GBufferRenderer(rhi::Device* device, rhi::Swapchain* swapchain,
                                 InstanceMgr& static_instance_mgr, RenderGraph& rg)
    : device_(device), swapchain_(swapchain), static_instance_mgr_(static_instance_mgr), rg_(rg) {}

GBufferRenderer::~GBufferRenderer() = default;

void GBufferRenderer::bake(const GeometryBatch& draw_batch, GbufferPassInfo& gbuffer_pass_info,
                           const TaskCmdBufRgIdsPerView& task_cmd_buf_rg_ids,
                           DrawCullPhase cull_phase, RGResourceId& meshlet_vis_id,
                           RGResourceId& draw_cnt_buf_id, RGResourceId& final_depth_pyramid_id,
                           RGResourceId& meshlet_draw_stats_buf_id, RenderView& render_view,
                           rhi::BufferHandle materials_buf_handle,
                           const IdxOffset& frame_globals_buf_info) {
  bool late = cull_phase == DrawCullPhase::Late;
  auto& p = rg_.add_graphics_pass(late ? "gbuffer_late" : "gbuffer_early");
  RGResourceId task_cmd_buf_rg_handles[AlphaMaskType::Count]{};
  RGResourceId out_draw_count_buf_rg_handle{};

  if (get_ctx().settings.mesh_shaders_enabled) {
    for (size_t alpha_mask_type = 0; alpha_mask_type < AlphaMaskType::Count; alpha_mask_type++) {
      if (draw_batch.get_stats().vertex_count > 0) {
        auto id =
            task_cmd_buf_rg_ids.phase(cull_phase)[static_cast<AlphaMaskType>(alpha_mask_type)];
        ASSERT(id.is_valid());
        task_cmd_buf_rg_handles[alpha_mask_type] =
            p.read_buf(id, PipelineStage::MeshShader | PipelineStage::TaskShader);
      }
    }
    out_draw_count_buf_rg_handle = p.read_buf(draw_cnt_buf_id, PipelineStage::TaskShader);
    if (late) {
      p.sample_tex(final_depth_pyramid_id, PipelineStage::TaskShader);
      meshlet_vis_id = p.rw_buf(meshlet_vis_id, PipelineStage::TaskShader);
    } else {
      ASSERT(static_instance_mgr_.get_num_meshlet_vis_buf_elements() > 0);
      meshlet_vis_id = rg_.create_buffer(
          {.size = sizeof(uint32_t) * static_instance_mgr_.get_num_meshlet_vis_buf_elements(),
           .defer_reuse = true},
          "meshlet_vis_buf");
      p.write_buf(meshlet_vis_id, PipelineStage::TaskShader);
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
    meshlet_draw_stats_buf_id =
        p.rw_buf(meshlet_draw_stats_buf_id, rhi::PipelineStage::TaskShader);
    meshlet_stats_for_pass = meshlet_draw_stats_buf_id;
  }

  auto dep_id = gbuffer_pass_info.depth_id;
  p.set_ex([this, rg_gbuffer_a_handle = gbuffer_pass_info.gbuffer_a_id,
            rg_gbuffer_b_handle = gbuffer_pass_info.gbuffer_b_id, dep_id, late, &render_view,
            meshlet_vis_id, meshlet_stats_for_pass, &draw_batch, materials_buf_handle,
            &frame_globals_buf_info, out_draw_count_buf_rg_handle, cull_phase,
            task_cmd_buf_rg_ids = task_cmd_buf_rg_ids.phase(cull_phase)](rhi::CmdEncoder* enc) {
    if (!static_instance_mgr_.has_draws()) {
      return;
    }
    auto depth_handle = rg_.get_att_img(dep_id);
    ASSERT(depth_handle.is_valid());
    auto gbuffer_a_tex = rg_.get_att_img(rg_gbuffer_a_handle);
    auto gbuffer_b_tex = rg_.get_att_img(rg_gbuffer_b_handle);
    auto load_op = late ? rhi::LoadOp::Load : rhi::LoadOp::Clear;
    enc->begin_rendering({
        RenderAttInfo::color_att(gbuffer_a_tex, load_op, {.color = glm::vec4(0, 0, 0, 0)}),
        RenderAttInfo::color_att(gbuffer_b_tex, load_op, {.color = glm::vec4(0, 0, 0, 0)}),
        RenderAttInfo::depth_stencil_att(
            depth_handle, load_op,
            {.depth_stencil = {.depth = get_ctx().settings.reverse_z ? 0.f : 1.f}}),
    });
    enc->bind_srv(materials_buf_handle, 11);

    if (get_ctx().settings.mesh_shaders_enabled) {
      encode_mesh_shader_pass(enc, meshlet_vis_id, cull_phase, render_view, meshlet_stats_for_pass,
                              frame_globals_buf_info, depth_handle, draw_batch, task_cmd_buf_rg_ids,
                              rg_.get_buf(out_draw_count_buf_rg_handle));
    } else {
      ASSERT(0 && "Not implemented");
      // enc->bind_pipeline(test2_pso_);
      // bool use_indirect = false;
      // if (use_indirect) {
      //   ASSERT(!indirect_cmd_buf_ids_.empty());
      //   enc->draw_indexed_indirect(static_instance_mgr_.get_draw_cmd_buf(),
      //                              indirect_cmd_buf_ids_[0],
      //                              static_instance_mgr_.stats().max_instance_data_count, 0);

      // } else {
      //   ASSERT(indirect_cmd_buf_ids_.size());
      //   BasicIndirectPC pc{
      //       .view_data_buf_idx = get_render_view(view_id).data_buf_info.idx,
      //       .view_data_buf_offset = get_render_view(view_id).data_buf_info.offset_bytes,
      //       .vert_buf_idx = static_draw_batch_.vertex_buf.get_buffer()->bindless_idx(),
      //       .instance_data_buf_idx =
      //           device_->get_buf(static_instance_mgr_.get_instance_data_buf())->bindless_idx(),
      //       .mat_buf_idx = materials_buf_.get_buffer()->bindless_idx(),
      //   };
      //   enc->push_constants(&pc, sizeof(pc));
      //   for (const auto& cmd : static_instance_mgr_.cpu_draw_cmds()) {
      //     // TODO: make no indirect a toggleable option.
      //     enc->draw_indexed_primitives(rhi::PrimitiveTopology::TriangleList,
      //                                  static_draw_batch_.index_buf.get_buffer_handle(),
      //                                  cmd.first_index * sizeof(DefaultIndexT), cmd.index_count,
      //                                  1, cmd.vertex_offset / sizeof(DefaultVertex),
      //                                  cmd.first_instance, rhi::IndexType::Uint32);
      //   }
      // }
    }
    enc->end_rendering();
  });
}
void GBufferRenderer::load_pipelines(ShaderManager& shader_mgr) {
  test2_pso_ = shader_mgr.create_graphics_pipeline({
      .shaders = {{{"basic_indirect", ShaderType::Vertex},
                   {"basic_indirect", ShaderType::Fragment}}},
  });

  // TODO: only is mesh shaders enabled
  // if (true) {
  for (size_t alpha_mask_type = 0; alpha_mask_type < AlphaMaskType::Count; alpha_mask_type++) {
    gbuffer_meshlet_psos_[alpha_mask_type] = shader_mgr.create_graphics_pipeline({
        .shaders = {{{"forward_meshlet", ShaderType::Task},
                     {"gbuffer_meshlet", ShaderType::Mesh},
                     {alpha_mask_type == AlphaMaskType::Mask ? "forward_meshlet_alphatest"
                                                             : "forward_meshlet",
                      ShaderType::Fragment}}},
    });
  }
  // }
}

void GBufferRenderer::encode_mesh_shader_pass(
    rhi::CmdEncoder* enc, const RGResourceId& meshlet_vis_id, DrawCullPhase cull_phase,
    const RenderView& render_view, RGResourceId meshlet_draw_stats_buf_id,
    const IdxOffset& frame_globals_buf_info, rhi::TextureHandle depth_handle,
    const GeometryBatch& draw_batch, TaskCmdBufRgIdsByAlphaMask task_cmd_buf_rg_ids,
    rhi::BufferHandle out_draw_count_buf) const {
  enc->set_depth_stencil_state(
      get_ctx().settings.reverse_z ? rhi::CompareOp::Greater : rhi::CompareOp::Less, true);
  enc->set_wind_order(rhi::WindOrder::CounterClockwise);
  enc->set_cull_mode(rhi::CullMode::Back);
  enc->set_viewport({0, 0}, device_->get_tex(depth_handle)->desc().dims);

  ASSERT(get_ctx().settings.mesh_shaders_enabled);

  enc->bind_uav(rg_.get_buf(meshlet_vis_id), 1);
  enc->bind_uav(rg_.get_buf(meshlet_draw_stats_buf_id), 2);
  if (cull_phase == DrawCullPhase::Late) {
    enc->bind_srv(render_view.depth_pyramid_tex.handle, 3);
  }
  enc->bind_srv(draw_batch.mesh_buf.get_buffer_handle(), 5);
  enc->bind_srv(draw_batch.meshlet_buf.get_buffer_handle(), 6);
  enc->bind_srv(draw_batch.meshlet_triangles_buf.get_buffer_handle(), 7);
  enc->bind_srv(draw_batch.meshlet_vertices_buf.get_buffer_handle(), 8);
  enc->bind_srv(draw_batch.vertex_buf.get_buffer_handle(), 9);
  enc->bind_srv(static_instance_mgr_.get_instance_data_buf(), 10);
  enc->bind_cbv(frame_globals_buf_info.buf, GLOBALS_SLOT, frame_globals_buf_info.offset_bytes);
  enc->bind_cbv(render_view.data_buf_info.buf, VIEW_DATA_SLOT,
                render_view.data_buf_info.offset_bytes);
  enc->bind_cbv(render_view.cull_data_buf_info.buf, 4, render_view.cull_data_buf_info.offset_bytes);
  Task2PC pc{
      .pass = static_cast<uint32_t>(cull_phase),
      .flags = 0,
      .out_draw_count_buf_idx = device_->get_buf(out_draw_count_buf)->bindless_idx(),
  };
  if (get_ctx().settings.meshlet_frustum_culling_enabled && get_ctx().settings.culling_enabled) {
    pc.flags |= MESHLET_FRUSTUM_CULL_ENABLED_BIT;
  }
  if (get_ctx().settings.meshlet_cone_culling_enabled && get_ctx().settings.culling_enabled) {
    pc.flags |= MESHLET_CONE_CULL_ENABLED_BIT;
  }
  if (get_ctx().settings.meshlet_occlusion_culling_enabled && get_ctx().settings.culling_enabled) {
    pc.flags |= MESHLET_OCCLUSION_CULL_ENABLED_BIT;
  }

  for (size_t alpha_mask_type = 0; alpha_mask_type < AlphaMaskType::Count; alpha_mask_type++) {
    auto task_cmd_buf_handle =
        rg_.get_buf(task_cmd_buf_rg_ids[static_cast<AlphaMaskType>(alpha_mask_type)]);
    enc->bind_srv(task_cmd_buf_handle, 4);
    enc->bind_pipeline(gbuffer_meshlet_psos_[alpha_mask_type]);
    pc.alpha_test_enabled = alpha_mask_type;
    enc->push_constants(&pc, sizeof(pc));
    enc->draw_mesh_threadgroups_indirect(out_draw_count_buf, alpha_mask_type * sizeof(uint32_t) * 3,
                                         {K_TASK_TG_SIZE, 1, 1}, {K_MESH_TG_SIZE, 1, 1});
  }
}

}  // namespace gfx

}  // namespace TENG_NAMESPACE