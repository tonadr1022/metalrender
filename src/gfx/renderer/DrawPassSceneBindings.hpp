#pragma once

#include <span>

#include "core/Config.hpp"
#include "gfx/renderer/RenderView.hpp"
#include "gfx/renderer/TaskCmdBufRgIds.hpp"
#include "gfx/rhi/GFXTypes.hpp"

namespace TENG_NAMESPACE::gfx {

struct GeometryBatch;
class InstanceMgr;

struct DrawPassSceneBindings {
  const GeometryBatch& draw_batch;
  rhi::BufferHandle materials_buf;
  IdxOffset frame_globals_buf_info{};
};

struct ViewRgIds {
  RGResourceId& meshlet_vis;
  RGResourceId& draw_count;
  RGResourceId& final_depth_pyramid;
  RGResourceId& meshlet_draw_stats;
};

struct ViewBindingsMeshlet {
  const TaskCmdBufRgIdsPerView& task_cmd_buf_rg_ids;
  RenderView& render_view;
  ViewRgIds rg_ids;
};

struct MeshletMeshPassView {
  const RenderView& render_view;
  RGResourceId meshlet_vis{};
  RGResourceId meshlet_draw_stats{};
  TaskCmdBufRgIdsByAlphaMask task_cmd_buf_rg_ids{};
  rhi::BufferHandle out_draw_count_buf;
};

void encode_meshlet_mesh_draw_pass(
    bool reverse_z, rhi::Device* device, RenderGraph& rg, InstanceMgr& static_instance_mgr,
    rhi::CmdEncoder* enc, DrawCullPhase cull_phase, bool enable_meshlet_occlusion_cull,
    rhi::TextureHandle depth_handle, const DrawPassSceneBindings& scene,
    const MeshletMeshPassView& mesh_pass,
    std::span<const rhi::PipelineHandleHolder> meshlet_psos_by_alpha_mask);

}  // namespace TENG_NAMESPACE::gfx