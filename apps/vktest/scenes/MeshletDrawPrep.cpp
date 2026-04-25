#include "MeshletDrawPrep.hpp"

#include <array>
#include <string>
#include <vector>

#include "core/Util.hpp"
#include "gfx/ModelGPUManager.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/ShaderManager.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/Device.hpp"
#include "hlsl/shared_cull_data.h"
#include "hlsl/shared_debug_meshlet_prepare.h"
#include "hlsl/shared_globals.h"
#include "hlsl/shared_task_cmd.h"
#include "hlsl/shared_test_clear_buf.h"

namespace teng::gfx {

using namespace rhi;

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
    std::string_view pass_name, uint32_t max_draws, bool late, bool gpu_object_frustum_cull,
    bool gpu_object_occlusion_cull, const BufferSuballoc& view_cb_suballoc,
    const BufferSuballoc& cull_cb, RGResourceId& task_cmd_rg, RGResourceId& indirect_args_rg,
    RGResourceId& visible_object_count_rg, RGResourceId* instance_vis_current_rg,
    RGResourceId* final_depth_pyramid_rg, rhi::TextureHandle final_depth_pyramid_tex) {
  auto& p = rg_.add_compute_pass(pass_name);
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

MeshletDrawPrep::MeshletDrawPrep(rhi::Device& device, RenderGraph& rg, ModelGPUMgr& model_gpu_mgr,
                                 ShaderManager& shader_mgr)
    : generate_task_cmd_compute_pass_(device, rg, model_gpu_mgr, shader_mgr),
      device_(device),
      rg_(rg) {
  clear_mesh_indirect_pso_ = shader_mgr.create_compute_pipeline(
      {.path = "test_clear_cnt_buf", .type = rhi::ShaderType::Compute});
}

MeshletDrawPrep::PassBuffers MeshletDrawPrep::create_pass_buffers(
    std::string_view label, size_t task_cmd_count, RGResourceId visible_object_count_rg) {
  const std::string name(label);
  return {
      .task_cmd_rg =
          rg_.create_buffer({.size = task_cmd_count * sizeof(TaskCmd)}, name + "_task_cmds"),
      .indirect_args_rg = rg_.create_buffer({.size = k_indirect_bytes}, name + "_indirect_args"),
      .visible_object_count_rg = visible_object_count_rg.is_valid()
                                     ? visible_object_count_rg
                                     : create_visible_count_buffer(name + "_visible_object_count"),
  };
}

RGResourceId MeshletDrawPrep::create_visible_count_buffer(std::string_view label) {
  return rg_.create_buffer({.size = sizeof(uint32_t), .defer_reuse = true}, std::string(label));
}

RGResourceId MeshletDrawPrep::create_instance_visibility_buffer(uint32_t max_draws,
                                                                std::string_view label) {
  const size_t required = static_cast<size_t>(max_draws) * sizeof(uint32_t);
  return rg_.create_buffer(
      {.size = required, .temporal = true, .temporal_slot_mode = TemporalSlotMode::SingleSlot},
      std::string(label));
}

void MeshletDrawPrep::prime_instance_visibility(RGResourceId& instance_vis_rg, uint32_t max_draws,
                                                std::string_view pass_name) {
  if (rg_.has_history(instance_vis_rg)) {
    return;
  }
  const size_t required = static_cast<size_t>(max_draws) * sizeof(uint32_t);
  auto& p = rg_.add_transfer_pass(pass_name);
  instance_vis_rg = p.write_buf(instance_vis_rg, rhi::PipelineStage::AllTransfer);
  const RGResourceId instance_vis_id = instance_vis_rg;
  p.set_ex([this, required, instance_vis_id](CmdEncoder* enc) {
    enc->fill_buffer(rg_.get_buf(instance_vis_id), 0, static_cast<uint32_t>(required), 1);
  });
}

void MeshletDrawPrep::clear_indirect_args(std::string_view pass_name,
                                          std::span<RGResourceId> indirect_args) {
  if (indirect_args.empty()) {
    return;
  }

  auto& p = rg_.add_compute_pass(pass_name);
  for (RGResourceId& id : indirect_args) {
    id = p.write_buf(id, rhi::PipelineStage::ComputeShader);
  }
  std::vector<RGResourceId> ids(indirect_args.begin(), indirect_args.end());
  p.set_ex([this, ids](CmdEncoder* enc) {
    enc->bind_pipeline(clear_mesh_indirect_pso_);
    for (RGResourceId id : ids) {
      TestClearBufPC pc{.buf_idx = device_.get_buf(rg_.get_buf(id))->bindless_idx()};
      enc->push_constants(&pc, sizeof(pc));
      enc->dispatch_compute({static_cast<uint32_t>(AlphaMaskType::Count), 1u, 1u}, {1u, 1u, 1u});
    }
  });
}

void MeshletDrawPrep::clear_visible_count(RGResourceId& visible_count_rg,
                                          std::string_view pass_name) {
  auto& p = rg_.add_transfer_pass(pass_name);
  visible_count_rg = p.write_buf(visible_count_rg, rhi::PipelineStage::AllTransfer);
  p.set_ex([this, visible_count_rg](CmdEncoder* enc) {
    enc->fill_buffer(rg_.get_buf(visible_count_rg), 0, sizeof(uint32_t), 0);
  });
}

void MeshletDrawPrep::clear_visible_count_and_stats(RGResourceId& visible_count_rg,
                                                    RGResourceId& stats_rg, size_t stats_bytes,
                                                    std::string_view pass_name) {
  auto& p = rg_.add_transfer_pass(pass_name);
  visible_count_rg = p.write_buf(visible_count_rg, rhi::PipelineStage::AllTransfer);
  stats_rg = p.write_buf(stats_rg, rhi::PipelineStage::AllTransfer);
  p.set_ex([this, visible_count_rg, stats_rg, stats_bytes](CmdEncoder* enc) {
    enc->fill_buffer(rg_.get_buf(visible_count_rg), 0, sizeof(uint32_t), 0);
    enc->fill_buffer(rg_.get_buf(stats_rg), 0, stats_bytes, 0);
  });
}

void MeshletDrawPrep::bake_task_commands(const TaskRequest& req, PassBuffers& buffers) {
  generate_task_cmd_compute_pass_.bake(
      req.pass_name, req.max_draws, req.late, req.object_frustum_cull, req.object_occlusion_cull,
      req.view_cb, req.cull_cb, buffers.task_cmd_rg, buffers.indirect_args_rg,
      buffers.visible_object_count_rg, req.instance_vis_current_rg, req.final_depth_pyramid_rg,
      req.final_depth_pyramid_tex);
}

}  // namespace teng::gfx
