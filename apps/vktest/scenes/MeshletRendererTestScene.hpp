#pragma once

#include <array>
#include <vector>

#include "../../common/ScenePresets.hpp"
#include "../TestDebugScenes.hpp"
#include "FpsCameraController.hpp"
#include "gfx/RendererTypes.hpp"
#include "gfx/renderer/AlphaMaskType.hpp"
#include "gfx/rhi/Texture.hpp"
#include "hlsl/shared_csm.h"
#include "hlsl/shared_cull_data.h"
#include "hlsl/shared_globals.h"

namespace teng::gfx {

class RenderGraph;
class ModelGPUMgr;

class GenerateTaskCmdComputePass {
 public:
  GenerateTaskCmdComputePass(rhi::Device& device, RenderGraph& rg, ModelGPUMgr& model_gpu_mgr,
                             ShaderManager& shader_mgr);

  void bake(uint32_t max_draws, bool late, bool gpu_object_frustum_cull,
            bool gpu_object_occlusion_cull, const BufferSuballoc& view_cb_suballoc,
            const BufferSuballoc& cull_cb, RGResourceId& task_cmd_rg,
            RGResourceId& indirect_args_rg, RGResourceId& visible_object_count_rg,
            RGResourceId* instance_vis_current_rg, RGResourceId* final_depth_pyramid_rg,
            rhi::TextureHandle final_depth_pyramid_tex);

 private:
  rhi::PipelineHandleHolder prepare_meshlets_pso_;
  rhi::PipelineHandleHolder prepare_meshlets_late_pso_;
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

  void apply_demo_scene_preset(size_t index) override;

  [[nodiscard]] ViewData prepare_view_data();
  [[nodiscard]] CullData prepare_cull_data(const ViewData& vd) const;
  [[nodiscard]] CullData prepare_cull_data_late(const ViewData& vd) const;
  [[nodiscard]] CullData prepare_cull_data_for_proj(const glm::mat4& proj, float z_near,
                                                    float z_far) const;

  struct ShadowCascadeConfig {
    uint32_t max_cascades{CSM_MAX_CASCADES};
    uint32_t cascade_count{3};
    uint32_t shadow_map_resolution{2048};
    float z_near{0.1f};
    float z_far{200.f};
    float split_lambda{0.95f};
    float bias_min{0.0004f};
    float bias_max{0.0025f};
  };

  struct ShadowFrameData {
    uint32_t cascade_count{};
    CSMData csm_data{};
    std::array<ViewData, CSM_MAX_CASCADES> view_data{};
    std::array<CullData, CSM_MAX_CASCADES> cull_data{};
  };
  [[nodiscard]] ShadowFrameData build_shadow_frame_data(const ViewData& camera_view,
                                                        const glm::vec3& toward_light) const;

  void render();
  void add_render_graph_passes() override;

 private:
  void load_scene_presets();
  void clear_all_models();
  void apply_preset(size_t idx);

  void make_depth_pyramid_tex();

  void update_toward_light_effective(const TestSceneContext& ctx);

  glm::vec3 toward_light_manual_{0.35f, 1.f, 0.4f};
  glm::vec3 toward_light_effective_{0.35f, 1.f, 0.4f};
  bool day_night_cycle_{false};
  bool day_night_cycle_paused_{false};
  float day_cycle_time_sec_{0.f};
  float day_cycle_period_sec_{120.f};

  bool reverse_z_{true};
  std::optional<GenerateTaskCmdComputePass> generate_task_cmd_compute_pass_;
  rhi::PipelineHandleHolder shade_pso_;
  std::array<rhi::PipelineHandleHolder, static_cast<size_t>(AlphaMaskType::Count)>
      meshlet_pso_early_;
  std::array<rhi::PipelineHandleHolder, static_cast<size_t>(AlphaMaskType::Count)>
      meshlet_pso_late_;
  std::array<rhi::PipelineHandleHolder, static_cast<size_t>(AlphaMaskType::Count)>
      meshlet_pso_shadow_;
  rhi::PipelineHandleHolder prepare_meshlets_pso_;
  rhi::PipelineHandleHolder prepare_meshlets_late_pso_;
  rhi::PipelineHandleHolder clear_mesh_indirect_pso_;
  rhi::PipelineHandleHolder depth_reduce_pso_;
  rhi::TexAndViewHolder depth_pyramid_tex_;
  rhi::BufferHandleHolder meshlet_vis_buf_;
  int debug_depth_pyramid_mip_{0};
  FpsCameraController fps_camera_;
  std::vector<ModelHandle> models_;
  std::vector<teng::demo_scenes::ScenePreset> scene_presets_;
  bool gpu_object_frustum_cull_{true};
  bool gpu_object_occlusion_cull_{true};
  std::array<rhi::BufferHandleHolder, k_max_frames_in_flight> task_cmd_group_count_readback_;
  std::array<rhi::BufferHandleHolder, k_max_frames_in_flight> visible_object_count_readback_;
  GPUFrameAllocator3 frame_uniform_gpu_allocator_;
  ShadowCascadeConfig shadow_cfg_{};
  std::array<rhi::TextureViewHandle, CSM_MAX_CASCADES> shadow_depth_layer_views_{-1, -1, -1, -1};
  rhi::TextureHandle cached_shadow_depth_tex_;
  bool visualize_shadow_cascades_{false};
  int debug_csm_cascade_layer_{0};
  uint32_t frame_num_{0};

  static constexpr size_t k_meshlet_draw_stats_bytes = sizeof(uint32_t) * 4;
  rhi::BufferHandleHolder meshlet_stats_buf_readback_[k_max_frames_in_flight];
};
}  // namespace teng::gfx
