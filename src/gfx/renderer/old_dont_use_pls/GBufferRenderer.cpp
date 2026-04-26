#include "GBufferRenderer.hpp"

#include "DrawPassSceneBindings.hpp"
#include "gfx/DrawBatch.hpp"
#include "gfx/ShaderManager.hpp"
#include "gfx/renderer/InstanceMgr.hpp"
#include "gfx/renderer/RendererCVars.hpp"
#include "gfx/renderer/old_dont_use_pls/TaskCmdBufRgIds.hpp"
#include "gfx/rhi/Pipeline.hpp"

namespace TENG_NAMESPACE::gfx {

using namespace rhi;

void GBufferRenderer::declare_indexed_indirect_gbuffer_barriers(RenderGraph::Pass& p,
                                                                RGResourceId indirect_cmds_rg) {
  p.read_buf(indirect_cmds_rg, PipelineStage::DrawIndirect, AccessFlags::IndirectCommandRead);
}

void GBufferRenderer::encode_indexed_indirect_gbuffer_pass(rhi::CmdEncoder* enc,
                                                           rhi::TextureHandle depth_handle,
                                                           uint32_t indirect_icb_id,
                                                           uint32_t max_draw_commands) const {
  enc->set_depth_stencil_state(reverse_z_ ? rhi::CompareOp::Greater : rhi::CompareOp::Less, true);
  enc->set_wind_order(rhi::WindOrder::CounterClockwise);
  enc->set_cull_mode(rhi::CullMode::Back);
  enc->set_viewport({0, 0}, device_->get_tex(depth_handle)->desc().dims);
  enc->bind_pipeline(gbuffer_indexed_indirect_pso_);
  enc->draw_indexed_indirect(static_instance_mgr_.get_draw_cmd_buf(), indirect_icb_id,
                             max_draw_commands, 0);
}

// draw_batch / render_view pointers must stay valid until this pass's ExecuteFn runs.
GBufferRenderer::GBufferRenderer(rhi::Device* device, InstanceMgr& static_instance_mgr,
                                 RenderGraph& rg, bool reverse_z)
    : device_(device), static_instance_mgr_(static_instance_mgr), rg_(rg), reverse_z_(reverse_z) {}

GBufferRenderer::~GBufferRenderer() = default;

void GBufferRenderer::bake(PassInfo& gbuffer_pass_info, DrawCullPhase cull_phase,
                           const DrawPassSceneBindings& scene,
                           [[maybe_unused]] const ViewBindings& view,
                           IndexedIndirectView indexed_indirect) {
  bool late = cull_phase == DrawCullPhase::Late;
  auto& p = rg_.add_graphics_pass(late ? "gbuffer_late" : "gbuffer_early");
  declare_indexed_indirect_gbuffer_barriers(p, indexed_indirect.indirect_cmds_rg);

  gbuffer_pass_info.gbuffer_a_id =
      rg_.create_texture({.format = rhi::TextureFormat::R16G16B16A16Sfloat}, "gbuffer_a");
  gbuffer_pass_info.gbuffer_b_id =
      rg_.create_texture({.format = rhi::TextureFormat::R8G8B8A8Unorm}, "gbuffer_b");
  p.write_color_output(gbuffer_pass_info.gbuffer_a_id);
  p.write_color_output(gbuffer_pass_info.gbuffer_b_id);
  gbuffer_pass_info.depth_id =
      rg_.create_texture({.format = rhi::TextureFormat::D32float}, "depth_tex");
  p.write_depth_output(gbuffer_pass_info.depth_id);

  p.set_ex([this, late, rg_a = gbuffer_pass_info.gbuffer_a_id,
            rg_b = gbuffer_pass_info.gbuffer_b_id, rg_depth = gbuffer_pass_info.depth_id,
            materials = scene.materials_buf, indexed_indirect](rhi::CmdEncoder* enc) {
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
        RenderAttInfo::depth_stencil_att(depth_handle, load_op,
                                         {.depth_stencil = {.depth = reverse_z_ ? 0.f : 1.f}}),
    });
    enc->bind_srv(materials, 11);
    encode_indexed_indirect_gbuffer_pass(enc, depth_handle, indexed_indirect.indirect_icb_id,
                                         indexed_indirect.max_draw_commands);
    enc->end_rendering();
  });
}

void GBufferRenderer::bake(PassInfo& gbuffer_pass_info, DrawCullPhase cull_phase,
                           const DrawPassSceneBindings& scene, const ViewBindingsMeshlet& view) {
  bool late = cull_phase == DrawCullPhase::Late;
  auto& p = rg_.add_graphics_pass(late ? "gbuffer_late" : "gbuffer_early");
  RGResourceId out_draw_count_buf_rg_handle{};
  ASSERT(renderer_cv::pipeline_mesh_shaders.get() != 0);

  // When object occlusion is off, we still need the Late task cmd buffer when meshlet occlusion
  // is enabled; forward_meshlet's late pass depends on `pass != 0` task data.
  DrawCullPhase task_cmd_buf_phase;
  if (renderer_cv::culling_object_occlusion.get() != 0 ||
      renderer_cv::culling_meshlet_occlusion.get() != 0) {
    task_cmd_buf_phase = cull_phase;
  } else {
    task_cmd_buf_phase = DrawCullPhase::Early;
  }
  for (size_t alpha_mask_type = 0; alpha_mask_type < AlphaMaskType::Count; alpha_mask_type++) {
    if (scene.draw_batch.get_stats().vertex_count > 0) {
      // use phase 0 for task cmd buf ids if object occlusion is disabled, since only one pass for
      // generating them.

      auto id = view.task_cmd_buf_rg_ids.phase(
          task_cmd_buf_phase)[static_cast<AlphaMaskType>(alpha_mask_type)];
      ASSERT(id.is_valid());
      p.read_buf(id, PipelineStage::MeshShader | PipelineStage::TaskShader);
    }
  }
  out_draw_count_buf_rg_handle = p.read_buf(*view.rg_ids.draw_count, PipelineStage::TaskShader);
  if (late) {
    p.sample_tex(*view.rg_ids.final_depth_pyramid, PipelineStage::TaskShader,
                 RgSubresourceRange::all_mips_all_slices());
    *view.rg_ids.meshlet_vis = p.rw_buf(*view.rg_ids.meshlet_vis, PipelineStage::TaskShader);
  } else {
    // Early meshlet pass reads per-meshlet history from this UAV; track read access for
    // backends that require explicit read hazards (notably Vulkan).
    *view.rg_ids.meshlet_vis = p.rw_buf(*view.rg_ids.meshlet_vis, PipelineStage::TaskShader);
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
  *view.rg_ids.meshlet_draw_stats =
      p.rw_buf(*view.rg_ids.meshlet_draw_stats, rhi::PipelineStage::TaskShader);
  RGResourceId meshlet_stats_for_pass = *view.rg_ids.meshlet_draw_stats;

  const RGResourceId meshlet_vis_for_pass = *view.rg_ids.meshlet_vis;
  p.set_ex(
      [this, late, cull_phase, rg_a = gbuffer_pass_info.gbuffer_a_id,
       rg_b = gbuffer_pass_info.gbuffer_b_id, rg_depth = gbuffer_pass_info.depth_id,
       rv = &view.render_view, meshlet_vis_rg = meshlet_vis_for_pass,
       meshlet_stats_rg = meshlet_stats_for_pass, batch = &scene.draw_batch,
       materials = scene.materials_buf, frame_globals = scene.frame_globals_buf_info,
       out_draw_count_rg = out_draw_count_buf_rg_handle,
       task_cmd_rg = view.task_cmd_buf_rg_ids.phase(task_cmd_buf_phase)](rhi::CmdEncoder* enc) {
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
            RenderAttInfo::depth_stencil_att(depth_handle, load_op,
                                             {.depth_stencil = {.depth = reverse_z_ ? 0.f : 1.f}}),
        });
        enc->bind_srv(materials, 11);
        const DrawPassSceneBindings mesh_scene{*batch, materials, frame_globals};
        const MeshletMeshPassView mesh_pass{*rv, meshlet_vis_rg, meshlet_stats_rg, task_cmd_rg,
                                            rg_.get_buf(out_draw_count_rg)};
        encode_meshlet_mesh_draw_pass(
            reverse_z_, device_, rg_, static_instance_mgr_, enc, cull_phase, true, depth_handle,
            mesh_scene, mesh_pass,
            std::span<const rhi::PipelineHandleHolder>(gbuffer_meshlet_psos_[late],
                                                       static_cast<size_t>(AlphaMaskType::Count)));
        enc->end_rendering();
      });
}

void GBufferRenderer::load_pipelines(ShaderManager& shader_mgr) {
  gbuffer_indexed_indirect_pso_ = shader_mgr.create_graphics_pipeline({
      .shaders = {{{"basic_indirect", ShaderType::Vertex},
                   {"basic_indirect", ShaderType::Fragment}}},
      .rendering = {.color_formats = {rhi::TextureFormat::R16G16B16A16Sfloat,
                                      rhi::TextureFormat::R8G8B8A8Unorm},
                    .depth_format = rhi::TextureFormat::D32float},
      .depth_stencil = rhi::GraphicsPipelineCreateInfo::depth_enable(true, rhi::CompareOp::Less),
  });

  for (size_t alpha_mask_type = 0; alpha_mask_type < AlphaMaskType::Count; alpha_mask_type++) {
    for (size_t late = 0; late < 2; late++) {
      gbuffer_meshlet_psos_[late][alpha_mask_type] = shader_mgr.create_graphics_pipeline({
          .shaders = {{{late ? "forward_meshlet_late" : "forward_meshlet", ShaderType::Task},
                       {"gbuffer_meshlet", ShaderType::Mesh},
                       {alpha_mask_type == AlphaMaskType::Mask ? "forward_meshlet_alphatest"
                                                               : "forward_meshlet_gbuffer",
                        ShaderType::Fragment}}},
          .name = late ? "gbuffer_meshlet_late" : "gbuffer_meshlet_early",
      });
    }
  }
}

}  // namespace TENG_NAMESPACE::gfx
