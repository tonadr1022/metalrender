#include "DrawPassSceneBindings.hpp"

#include "core/Logger.hpp"
#include "gfx/DrawBatch.hpp"
#include "gfx/renderer/InstanceMgr.hpp"
#include "gfx/renderer/RendererCVars.hpp"
#include "gfx/renderer/TaskCmdBufRgIds.hpp"
#include "gfx/rhi/Device.hpp"
#include "hlsl/shader_constants.h"
#include "hlsl/shared_cull_data.h"
#include "hlsl/shared_forward_meshlet.h"
#include "hlsl/shared_globals.h"

namespace TENG_NAMESPACE::gfx {

void encode_meshlet_mesh_draw_pass(
    bool reverse_z, rhi::Device* device, RenderGraph& rg, InstanceMgr& static_instance_mgr,
    rhi::CmdEncoder* enc, DrawCullPhase cull_phase, bool enable_meshlet_occlusion_cull,
    rhi::TextureHandle depth_handle, const DrawPassSceneBindings& scene,
    const MeshletMeshPassView& mesh_pass,
    std::span<const rhi::PipelineHandleHolder> meshlet_psos_by_alpha_mask) {
  enc->set_depth_stencil_state(reverse_z ? rhi::CompareOp::Greater : rhi::CompareOp::Less, true);
  enc->set_wind_order(rhi::WindOrder::CounterClockwise);
  enc->set_cull_mode(rhi::CullMode::Back);
  enc->set_viewport({0, 0}, device->get_tex(depth_handle)->desc().dims);

  ASSERT(renderer_cv::pipeline_mesh_shaders.get() != 0);
  ASSERT(meshlet_psos_by_alpha_mask.size() == static_cast<size_t>(AlphaMaskType::Count));

  if (enable_meshlet_occlusion_cull) {
    enc->bind_uav(rg.get_external_buffer(mesh_pass.meshlet_vis), 1);
    if (cull_phase == DrawCullPhase::Late) {
      enc->bind_srv(mesh_pass.render_view.depth_pyramid_tex.handle, 3);
    }
  }

  enc->bind_uav(rg.get_buf(mesh_pass.meshlet_draw_stats), 2);
  enc->bind_srv(scene.draw_batch.mesh_buf.get_buffer_handle(), 5);
  enc->bind_srv(scene.draw_batch.meshlet_buf.get_buffer_handle(), 6);
  enc->bind_srv(scene.draw_batch.meshlet_triangles_buf.get_buffer_handle(), 7);
  enc->bind_srv(scene.draw_batch.meshlet_vertices_buf.get_buffer_handle(), 8);
  enc->bind_srv(scene.draw_batch.vertex_buf.get_buffer_handle(), 9);
  enc->bind_srv(static_instance_mgr.get_instance_data_buf(), 10);
  enc->bind_cbv(scene.frame_globals_buf_info.buf, GLOBALS_SLOT,
                scene.frame_globals_buf_info.offset_bytes, sizeof(GlobalData));
  enc->bind_cbv(mesh_pass.render_view.data_buf_info.buf, VIEW_DATA_SLOT,
                mesh_pass.render_view.data_buf_info.offset_bytes, sizeof(ViewData));
  enc->bind_cbv(mesh_pass.render_view.cull_data_buf_info.buf, 4,
                mesh_pass.render_view.cull_data_buf_info.offset_bytes, sizeof(CullData));
  Task2PC pc{
      .flags = 0,
      .out_draw_count_buf_idx = device->get_buf(mesh_pass.out_draw_count_buf)->bindless_idx(),
  };
  if (renderer_cv::culling_meshlet_frustum.get() != 0 && renderer_cv::culling_enabled.get() != 0) {
    pc.flags |= MESHLET_FRUSTUM_CULL_ENABLED_BIT;
  }
  if (renderer_cv::culling_meshlet_cone.get() != 0 && renderer_cv::culling_enabled.get() != 0) {
    pc.flags |= MESHLET_CONE_CULL_ENABLED_BIT;
  }
  if (enable_meshlet_occlusion_cull && renderer_cv::culling_meshlet_occlusion.get() != 0 &&
      renderer_cv::culling_enabled.get() != 0) {
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

}  // namespace TENG_NAMESPACE::gfx