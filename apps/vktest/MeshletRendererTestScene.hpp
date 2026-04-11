#pragma once

#include "FpsCameraController.hpp"
#include "TestDebugScenes.hpp"
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

  void add_render_graph_passes() override;

 private:
  void recreate_meshlet_pso();
  void flush_pending_model_textures(ModelGPUMgr& mgr, rhi::Device& device,
                                    GPUFrameAllocator3& staging, rhi::CmdEncoder* enc);

  rhi::PipelineHandleHolder meshlet_pso_;
  rhi::PipelineHandleHolder clear_indirect_pso_;
  rhi::PipelineHandleHolder prepare_meshlets_pso_;
  // TextureHandleHolder meshlet_depth_tex_;
  // BufferHandleHolder view_cb_bufs_[k_max_frames_in_flight];
  // BufferHandleHolder view_prepare_storage_buf_;
  rhi::BufferHandleHolder globals_cb_buf_;
  rhi::BufferHandleHolder cull_storage_buf_;
  FpsCameraController fps_camera_;
  ModelHandle test_model_handle_;
  InstanceMgr::Alloc instance_alloc_{};
  bool gpu_object_frustum_cull_{true};
  std::array<rhi::BufferHandleHolder, k_max_frames_in_flight> task_cmd_group_count_readback_{};
  std::array<rhi::BufferHandleHolder, k_max_frames_in_flight> visible_object_count_readback_{};
  GPUFrameAllocator3 frame_uniform_gpu_allocator_;
  uint32_t meshlet_readback_frames_{0};
};
}  // namespace teng::gfx