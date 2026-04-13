#pragma once

#include <array>

#include "../TestDebugScenes.hpp"
#include "FpsCameraController.hpp"
#include "gfx/RendererTypes.hpp"
#include "gfx/renderer/AlphaMaskType.hpp"
#include "gfx/renderer/InstanceMgr.hpp"
#include "gfx/rhi/Texture.hpp"
#include "hlsl/shared_cull_data.h"
#include "hlsl/shared_globals.h"

namespace teng::gfx {

class RenderGraph;
class ModelGPUMgr;

class GenerateTaskCmdComputePass {
 public:
  GenerateTaskCmdComputePass(rhi::Device& device, RenderGraph& rg, ModelGPUMgr& model_gpu_mgr,
                             ShaderManager& shader_mgr);

  void bake(uint32_t max_draws, bool late, const BufferSuballoc& view_cb_suballoc,
            const BufferSuballoc& cull_cb, RGResourceId& task_cmd_rg,
            RGResourceId& indirect_args_rg, RGResourceId& visible_object_count_rg,
            RGResourceId& instance_vis_rg, RGResourceId* final_depth_pyramid_rg,
            rhi::TextureHandle final_depth_pyramid_tex, rhi::BufferHandle instance_vis_buf);

 private:
  rhi::PipelineHandleHolder prepare_meshlets_pso_;
  rhi::PipelineHandleHolder prepare_meshlets_late_pso_;
  bool gpu_object_occlusion_cull_{true};
  bool gpu_object_frustum_cull_{true};
  rhi::Device& device_;
  RenderGraph& rg_;
  ModelGPUMgr& model_gpu_mgr_;
};

class MeshletRendererScene final : public ITestScene {
 public:
  explicit MeshletRendererScene(const TestSceneContext& ctx);

  void shutdown() override;

  void on_frame(const TestSceneContext& ctx) override;

  void on_cursor_pos(double x, double y) override;

  void on_key_event(int key, int action, int mods) override;

  void on_imgui() override;

  void on_swapchain_resize() override;

  ViewData prepare_view_data();
  CullData prepare_cull_data(const ViewData& vd);
  CullData prepare_cull_data_late(const ViewData& vd);

  void render();
  void add_render_graph_passes() override;

 private:
  void make_depth_pyramid_tex();
  void ensure_meshlet_vis_buffer();

  bool reverse_z_{true};
  std::optional<GenerateTaskCmdComputePass> generate_task_cmd_compute_pass_;
  rhi::PipelineHandleHolder shade_pso_;
  std::array<rhi::PipelineHandleHolder, static_cast<size_t>(AlphaMaskType::Count)>
      meshlet_pso_early_;
  std::array<rhi::PipelineHandleHolder, static_cast<size_t>(AlphaMaskType::Count)>
      meshlet_pso_late_;
  rhi::PipelineHandleHolder prepare_meshlets_pso_;
  rhi::PipelineHandleHolder prepare_meshlets_late_pso_;
  rhi::PipelineHandleHolder clear_mesh_indirect_pso_;
  rhi::PipelineHandleHolder depth_reduce_pso_;
  rhi::TexAndViewHolder depth_pyramid_tex_;
  rhi::BufferHandleHolder meshlet_vis_buf_;
  int debug_depth_pyramid_mip_{0};
  FpsCameraController fps_camera_;
  ModelHandle test_model_handle_;
  InstanceMgr::Alloc instance_alloc_{};
  bool gpu_object_frustum_cull_{true};
  bool gpu_object_occlusion_cull_{true};
  rhi::BufferHandleHolder instance_vis_buf_;
  std::array<rhi::BufferHandleHolder, k_max_frames_in_flight> task_cmd_group_count_readback_;
  std::array<rhi::BufferHandleHolder, k_max_frames_in_flight> visible_object_count_readback_;
  GPUFrameAllocator3 frame_uniform_gpu_allocator_;
  uint32_t frame_num_{0};

  static constexpr size_t k_meshlet_draw_stats_bytes = sizeof(uint32_t) * 4;
  rhi::BufferHandleHolder meshlet_stats_buf_readback_[k_max_frames_in_flight];
};
}  // namespace teng::gfx
