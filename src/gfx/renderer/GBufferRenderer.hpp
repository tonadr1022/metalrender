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

}  // namespace rhi

namespace gfx {

class InstanceMgr;
class ShaderManager;
class RenderGraph;

class GBufferRenderer {
 public:
  explicit GBufferRenderer(rhi::Device* device, InstanceMgr& static_instance_mgr, RenderGraph& rg);
  ~GBufferRenderer();

  struct GbufferPassInfo {
    RGResourceId gbuffer_a_id{};
    RGResourceId gbuffer_b_id{};
    RGResourceId depth_id{};
  };

  struct GBufferViewRgIds {
    RGResourceId& meshlet_vis;
    RGResourceId& draw_count;
    RGResourceId& final_depth_pyramid;
    RGResourceId& meshlet_draw_stats;
  };

  struct GBufferViewBindings {
    const TaskCmdBufRgIdsPerView& task_cmd_buf_rg_ids;
    RenderView& render_view;
    GBufferViewRgIds rg_ids;
  };

  struct SceneBindings {
    const GeometryBatch& draw_batch;
    rhi::BufferHandle materials_buf;
    IdxOffset frame_globals_buf_info{};
  };

  struct MeshletMeshPassView {
    const RenderView& render_view;
    RGResourceId meshlet_vis{};
    RGResourceId meshlet_draw_stats{};
    TaskCmdBufRgIdsByAlphaMask task_cmd_buf_rg_ids{};
    rhi::BufferHandle out_draw_count_buf;
  };

  void bake(GbufferPassInfo& gbuffer_pass_info, DrawCullPhase cull_phase,
            const SceneBindings& scene, const GBufferViewBindings& view);
  void encode_mesh_shader_pass(rhi::CmdEncoder* enc, DrawCullPhase cull_phase,
                               rhi::TextureHandle depth_handle, const SceneBindings& scene,
                               const MeshletMeshPassView& mesh_pass) const;

  struct DepthOnlyPassInfo {};

  void load_pipelines(ShaderManager& shader_mgr);

 private:
  rhi::Device* device_;
  InstanceMgr& static_instance_mgr_;
  RenderGraph& rg_;
  rhi::PipelineHandleHolder gbuffer_meshlet_psos_[(size_t)AlphaMaskType::Count];
  rhi::PipelineHandleHolder test2_pso_;
};

}  // namespace gfx

}  // namespace TENG_NAMESPACE