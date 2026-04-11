#pragma once

#include "../TestDebugScenes.hpp"
#include "FpsCameraController.hpp"
#include "gfx/RendererTypes.hpp"
#include "gfx/renderer/InstanceMgr.hpp"
#include "hlsl/shared_cull_data.h"
#include "hlsl/shared_globals.h"

namespace teng::gfx {

class MeshletRendererScene final : public ITestScene {
 public:
  explicit MeshletRendererScene(const TestSceneContext& ctx);

  void shutdown() override;

  void on_frame(const TestSceneContext& ctx) override;

  void on_cursor_pos(double x, double y) override;

  void on_key_event(int key, int action, int mods) override;

  void on_imgui() override;

  void on_swapchain_resize() override { recreate_meshlet_pso(); }

  ViewData prepare_view_data();
  CullData prepare_cull_data(const ViewData& vd);

  void render();
  void add_render_graph_passes() override;

 private:
  void recreate_meshlet_pso();

  rhi::PipelineHandleHolder shade_pso_;
  rhi::PipelineHandleHolder meshlet_pso_;
  rhi::PipelineHandleHolder clear_indirect_pso_;
  rhi::PipelineHandleHolder prepare_meshlets_pso_;
  FpsCameraController fps_camera_;
  ModelHandle test_model_handle_;
  InstanceMgr::Alloc instance_alloc_{};
  bool gpu_object_frustum_cull_{true};
  std::array<rhi::BufferHandleHolder, k_max_frames_in_flight> task_cmd_group_count_readback_{};
  std::array<rhi::BufferHandleHolder, k_max_frames_in_flight> visible_object_count_readback_{};
  GPUFrameAllocator3 frame_uniform_gpu_allocator_;
  uint32_t frame_num_{0};
};
}  // namespace teng::gfx