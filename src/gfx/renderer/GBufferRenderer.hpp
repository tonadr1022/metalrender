#pragma once

#include "core/Config.hpp"
#include "gfx/DrawBatch.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/renderer/AlphaMaskType.hpp"
#include "gfx/renderer/RenderView.hpp"
#include "gfx/renderer/TaskCmdBufRgIds.hpp"

namespace TENG_NAMESPACE {

namespace rhi {

class Device;
class Swapchain;

}  // namespace rhi

namespace gfx {

class InstanceMgr;
class ShaderManager;
class RenderGraph;

class GBufferRenderer {
 public:
  explicit GBufferRenderer(rhi::Device* device, rhi::Swapchain* swapchain,
                           InstanceMgr& static_instance_mgr, RenderGraph& rg);
  ~GBufferRenderer();

  struct GbufferPassInfo {
    RGResourceId gbuffer_a_id{};
    RGResourceId gbuffer_b_id{};
    RGResourceId depth_id{};
  };

  void bake(const GeometryBatch& draw_batch, GbufferPassInfo& gbuffer_pass_info,
            const TaskCmdBufRgIdsPerView& task_cmd_buf_rg_ids, DrawCullPhase cull_phase,
            RGResourceId& meshlet_vis_id, RGResourceId& draw_cnt_buf_id,
            RGResourceId& final_depth_pyramid_id, RGResourceId& out_counts_id,
            RenderView& render_view, rhi::BufferHandle out_counts_buf_handle,
            rhi::BufferHandle materials_buf_handle, const IdxOffset& frame_globals_buf_info);
  void encode_mesh_shader_pass(rhi::CmdEncoder* enc, const RGResourceId& meshlet_vis_id,
                               DrawCullPhase cull_phase, const RenderView& render_view,
                               rhi::BufferHandle out_counts_buf_handle,
                               const IdxOffset& frame_globals_buf_info,
                               rhi::TextureHandle depth_handle, const GeometryBatch& draw_batch,
                               TaskCmdBufRgIdsByAlphaMask task_cmd_buf_rg_ids,
                               rhi::BufferHandle out_draw_count_buf) const;

  struct DepthOnlyPassInfo {};

  struct PerEarlyLatePassInfo {
    RGResourceId task_cmd_buf_rg_handles[AlphaMaskType::Count]{};
    RGResourceId out_draw_count_buf_rg_handle{};
    RGResourceId out_counts_id{};
    RGResourceId meshlet_vis_id{};
  };

  void load_pipelines(ShaderManager& shader_mgr);

 private:
  rhi::Device* device_;
  rhi::Swapchain* swapchain_;
  InstanceMgr& static_instance_mgr_;
  RenderGraph& rg_;
  rhi::PipelineHandleHolder gbuffer_meshlet_psos_[(size_t)AlphaMaskType::Count];
  rhi::PipelineHandleHolder test2_pso_;
};

}  // namespace gfx

}  // namespace TENG_NAMESPACE