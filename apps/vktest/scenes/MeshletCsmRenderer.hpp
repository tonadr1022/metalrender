#pragma once

#include <array>

#include "MeshletDrawPrep.hpp"
#include "gfx/RendererTypes.hpp"
#include "gfx/renderer/AlphaMaskType.hpp"
#include "gfx/rhi/Texture.hpp"
#include "hlsl/shared_csm.h"
#include "hlsl/shared_cull_data.h"
#include "hlsl/shared_globals.h"

namespace teng::gfx {

class ModelGPUMgr;
class RenderGraph;
class ShaderManager;
struct GPUFrameAllocator3;

namespace rhi {
class Device;
}

enum class MeshletShadowMode {
  None,
  CascadedShadowMaps,
};

class MeshletCsmRenderer {
 public:
  struct SceneDefaults {
    float z_near{0.1f};
    float z_far{200.f};
    uint32_t cascade_count{3};
    float split_lambda{0.95f};
  };

  struct Output {
    MeshletShadowMode mode{MeshletShadowMode::None};
    bool valid{};
    RGResourceId depth_rg{};
    BufferSuballoc csm_cb{};
    uint32_t cascade_count{};
    uint32_t sample_layer_count{};
  };

  struct BakeRequest {
    const ViewData& camera_view;
    glm::vec3 toward_light{};
    BufferSuballoc shadow_globals_cb{};
    uint32_t max_draws{};
    size_t task_cmd_count{};
    RGResourceId& meshlet_vis_rg;
    RGResourceId& meshlet_stats_rg;
    GPUFrameAllocator3& frame_uniform_allocator;
    MeshletDrawPrep& draw_prep;
  };

  MeshletCsmRenderer(rhi::Device& device, RenderGraph& rg, ModelGPUMgr& model_gpu_mgr,
                     ShaderManager& shader_mgr);

  void shutdown();
  void on_imgui();

  [[nodiscard]] bool enabled() const { return mode_ == MeshletShadowMode::CascadedShadowMaps; }
  [[nodiscard]] bool visualize_cascade_colors() const {
    return enabled() && visualize_shadow_cascades_;
  }

  void set_scene_defaults(float z_near, float z_far, uint32_t cascade_count, float split_lambda);
  void set_scene_defaults(const SceneDefaults& d) {
    set_scene_defaults(d.z_near, d.z_far, d.cascade_count, d.split_lambda);
  }

  Output bake(const BakeRequest& req);

 private:
  struct Config {
    uint32_t max_cascades{CSM_MAX_CASCADES};
    uint32_t cascade_count{3};
    uint32_t shadow_map_resolution{2048};
    float z_near{0.1f};
    float z_far{200.f};
    float split_lambda{0.95f};
    float min_light_depth_padding{10.f};
    float bias_min{0.0004f};
    float bias_max{0.0025f};
  };

  struct FrameData {
    uint32_t cascade_count{};
    CSMData csm_data{};
    std::array<ViewData, CSM_MAX_CASCADES> view_data{};
    std::array<CullData, CSM_MAX_CASCADES> cull_data{};
  };

  [[nodiscard]] FrameData build_frame_data(const ViewData& camera_view,
                                           const glm::vec3& toward_light) const;
  void ensure_layer_views(rhi::TextureHandle depth_img);
  void destroy_layer_views();

  Config cfg_{};
  MeshletShadowMode mode_{MeshletShadowMode::CascadedShadowMaps};
  bool visualize_shadow_cascades_{false};
  int debug_csm_cascade_layer_{0};

  std::array<rhi::PipelineHandleHolder, static_cast<size_t>(AlphaMaskType::Count)> shadow_psos_;
  std::array<rhi::TextureViewHandle, CSM_MAX_CASCADES> shadow_depth_layer_views_{-1, -1, -1, -1};
  rhi::TextureHandle cached_shadow_depth_tex_;

  rhi::Device& device_;
  RenderGraph& rg_;
  ModelGPUMgr& model_gpu_mgr_;
};

}  // namespace teng::gfx
