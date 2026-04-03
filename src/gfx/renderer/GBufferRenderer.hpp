#pragma once

#include <string_view>

#include "core/Config.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/renderer/AlphaMaskType.hpp"
#include "gfx/renderer/DrawPassSceneBindings.hpp"
#include "gfx/renderer/RenderView.hpp"
#include "gfx/renderer/TaskCmdBufRgIds.hpp"

namespace TENG_NAMESPACE {

namespace gfx::rhi {

class Device;

}  // namespace gfx::rhi

namespace gfx {

class InstanceMgr;
class ShaderManager;
class RenderGraph;

class GBufferRenderer {
 public:
  explicit GBufferRenderer(rhi::Device* device, InstanceMgr& static_instance_mgr, RenderGraph& rg,
                           bool reverse_z);
  ~GBufferRenderer();

  struct PassInfo {
    RGResourceId gbuffer_a_id{};
    RGResourceId gbuffer_b_id{};
    RGResourceId depth_id{};
  };

  struct ViewBindingsMeshlet {
    const TaskCmdBufRgIdsPerView& task_cmd_buf_rg_ids;
    RenderView& render_view;
    ViewRgIds rg_ids;
  };

  struct ViewBindings {
    RenderView& render_view;
  };

  struct IndexedIndirectView {
    const RenderView& render_view;
    RGResourceId indirect_cmds_rg{};
    uint32_t indirect_icb_id{};
    uint32_t max_draw_commands{};
  };

  void bake(PassInfo& gbuffer_pass_info, DrawCullPhase cull_phase,
            const DrawPassSceneBindings& scene, const ViewBindingsMeshlet& view);
  void bake(PassInfo& gbuffer_pass_info, DrawCullPhase cull_phase,
            const DrawPassSceneBindings& scene, const ViewBindings& view,
            IndexedIndirectView indexed_indirect);

  struct ShadowDepthPassInfo {
    RGResourceId depth_id{};
  };

  // void bake_shadow_depth(std::string_view pass_name, ShadowDepthPassInfo& out,
  //                        DrawCullPhase cull_phase, const DrawPassSceneBindings& scene,
  //                        const ViewBindingsMeshlet& view);

  struct DepthOnlyPassInfo {};

  void load_pipelines(ShaderManager& shader_mgr);

 private:
  static void declare_indexed_indirect_gbuffer_barriers(RenderGraph::Pass& p,
                                                        RGResourceId indirect_cmds_rg);
  void encode_indexed_indirect_gbuffer_pass(rhi::CmdEncoder* enc, rhi::TextureHandle depth_handle,
                                            uint32_t indirect_icb_id,
                                            uint32_t max_draw_commands) const;

  rhi::Device* device_;
  InstanceMgr& static_instance_mgr_;
  RenderGraph& rg_;
  bool reverse_z_{};
  rhi::PipelineHandleHolder gbuffer_meshlet_psos_[2][(size_t)AlphaMaskType::Count];
  rhi::PipelineHandleHolder gbuffer_indexed_indirect_pso_;
};

}  // namespace gfx

}  // namespace TENG_NAMESPACE
