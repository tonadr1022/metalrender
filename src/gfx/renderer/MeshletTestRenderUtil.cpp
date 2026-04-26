#include "MeshletTestRenderUtil.hpp"

#include "core/EAssert.hpp"
#include "gfx/DrawBatch.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/renderer/InstanceMgr.hpp"
#include "gfx/renderer/old_dont_use_pls/TaskCmdBufRgIds.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/Device.hpp"
#include "hlsl/shader_constants.h"
#include "hlsl/shared_cull_data.h"
#include "hlsl/shared_forward_meshlet.h"
#include "hlsl/shared_globals.h"

namespace teng::gfx {

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

}  // namespace teng::gfx
