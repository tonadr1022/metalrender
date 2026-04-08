#pragma once

#include <array>
#include <filesystem>
#include <vector>

#include "ImGuiRenderer.hpp"
#include "core/Config.hpp"
#include "core/Math.hpp"  // IWYU pragma: keep
#include "gfx/BackedGPUAllocator.hpp"
#include "gfx/DrawBatch.hpp"
#include "gfx/GPUFrameAllocator2.hpp"
#include "gfx/ModelGPUManager.hpp"
#include "gfx/ModelInstance.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/RendererTypes.hpp"
#include "gfx/ShaderManager.hpp"
#include "gfx/renderer/BufferResize.hpp"
#include "gfx/renderer/InstanceMgr.hpp"
#include "gfx/renderer/ModelGPUUploader.hpp"
#include "gfx/renderer/RenderView.hpp"
#include "gfx/renderer/RendererCVars.hpp"
#include "gfx/rhi/Config.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "hlsl/shared_csm.h"

namespace TENG_NAMESPACE {

struct ImDrawData;
class Window;

namespace gfx {

namespace rhi {
class CmdEncoder;
}

class GBufferRenderer;
class CSMRenderer;

class MemeRenderer123 {
 public:
  struct CreateInfo {
    rhi::Device* device;
    rhi::Swapchain* swapchain;
    Window* window;
    std::filesystem::path resource_dir;
  };
  explicit MemeRenderer123(const CreateInfo& cinfo);
  ~MemeRenderer123();

  MemeRenderer123(const MemeRenderer123&) = delete;
  MemeRenderer123(MemeRenderer123&&) = delete;
  MemeRenderer123& operator=(const MemeRenderer123&) = delete;
  MemeRenderer123& operator=(MemeRenderer123&&) = delete;

  struct RenderArgs {
    glm::mat4 view_mat;
    glm::vec3 camera_pos;
    glm::vec4 clear_color;
    // bool draw_imgui;
  };
  void render(const RenderArgs& args);
  void draw_minimal_triangle();
  void on_imgui();
  bool on_key_event(int key, int action, int mods);
  void update_model_instance_transforms(const ModelInstance& model);
  bool mesh_shaders_enabled() const { return renderer_cv::pipeline_mesh_shaders.get() != 0; }
  void set_imgui_enabled(bool enabled) { renderer_cv::ui_imgui_enabled.set(enabled ? 1 : 0); }

  ModelGPUMgr* get_model_gpu_mgr() { return model_gpu_mgr_.get(); }
  const ModelGPUMgr* get_model_gpu_mgr() const { return model_gpu_mgr_.get(); }

  struct FinalDrawResults {
    uint32_t drawn_meshlets;
    uint32_t drawn_vertices;
  };
  // TODO: this is instance data mgr
  struct Stats {
    uint32_t total_instance_meshlets;
    uint32_t total_instance_vertices;
    uint32_t total_instances;
    FinalDrawResults draw_results;
    float gpu_frame_time_last_ms;
    float avg_gpu_frame_time;
  };

  const Stats& get_stats() const { return stats_; }

 private:
  void init_imgui();
  void shutdown_imgui();
  void meshlet_stats_imgui(size_t total_scene_models);
  void flush_pending_texture_uploads(rhi::CmdEncoder* enc);
  [[nodiscard]] uint32_t get_bindless_idx(const rhi::BufferHandleHolder& buf) const;
  [[nodiscard]] uint32_t get_bindless_idx(const rhi::BufferHandle& buf) const {
    return device_->get_buf(buf)->bindless_idx();
  }
  void add_render_graph_passes(const RenderArgs& args);

  RenderView& get_render_view(RenderViewId view_id) {
    ASSERT(view_id != RenderViewId::Invalid);
    return render_views_[(uint32_t)view_id];
  }
  RenderView& get_render_view(size_t view_i) {
    ASSERT(view_i < render_views_.size());
    return render_views_[view_i];
  }

  void clear_render_views() { render_views_.clear(); }

  RenderViewId create_render_view();
  void destroy_render_view(RenderViewId view_id);
  void make_depth_pyramid_tex(RenderViewId view_id, glm::uvec2 main_size);
  void reset_csm_debug_views();
  [[nodiscard]] uint32_t get_csm_debug_tex_idx(rhi::TextureHandle depth_tex, uint32_t cascade_idx);
  std::vector<RenderViewId> free_render_view_ids_;

  // guaranteed to be densely packed
  std::vector<RenderView> render_views_;
  RenderViewId main_render_view_id_{RenderViewId::Invalid};
  gch::small_vector<RenderViewId, CSM_MAX_CASCADES> shadow_map_render_views_;
  bool get_shadows_enabled() const { return renderer_cv::shadows_enabled.get() != 0; }
  void on_shadows_enabled_change(bool shadows_enabled);

  RGPass* clear_bufs_pass_{};

  // TODO: needs work
  void set_cull_data_and_globals(const RenderArgs& args);
  void recreate_swapchain_sized_textures();
  size_t prev_frame_idx() const {
    return curr_frame_idx_ == 0 ? device_->get_info().frames_in_flight - 1 : curr_frame_idx_ - 1;
  }
  size_t get_frames_ago_idx(size_t frames_ago) const {
    if (curr_frame_idx_ >= frames_ago) {
      return curr_frame_idx_ - frames_ago;
    }
    return device_->get_info().frames_in_flight + curr_frame_idx_ - frames_ago;
  }

  Stats stats_{};
  std::unique_ptr<gfx::ShaderManager> shader_mgr_;
  rhi::Device* device_{};
  Window* window_{};
  // rhi::PipelineHandleHolder test2_pso_;
  // rhi::PipelineHandleHolder gbuffer_meshlet_pso_;
  // rhi::PipelineHandleHolder gbuffer_meshlet_psos_[(size_t)AlphaMaskType::Count];
  rhi::PipelineHandleHolder draw_cull_pso_;
  rhi::PipelineHandleHolder reset_counts_buf_pso_;
  rhi::PipelineHandleHolder depth_reduce_pso_;
  rhi::PipelineHandleHolder shade_pso_;
  rhi::PipelineHandleHolder tex_only_pso_;
  rhi::PipelineHandleHolder tex_only_shadow_pso_;
  rhi::PipelineHandleHolder csm_no_frag_pso_;

  GPUFrameAllocator3 frame_gpu_upload_allocator_;
  GPUFrameAllocator3 frame_uniform_gpu_allocator_;
  BufferCopyMgr buffer_copy_mgr_;
  BackedGPUAllocator materials_buf_;

  size_t frame_num_{};
  size_t curr_frame_idx_{};

  std::filesystem::path resource_dir_;

  InstanceMgr static_instance_mgr_;
  GeometryBatch static_draw_batch_;

  BlockPool<ModelGPUHandle, ModelGPUResources> model_gpu_resource_pool_{20, 1, true};
  BlockPool<ModelInstanceGPUHandle, ModelInstanceGPUResources> model_instance_gpu_resource_pool_{
      1024, 5, true};

  gfx::RenderGraph rg_;

  std::optional<ImGuiRenderer> imgui_renderer_;

  std::vector<GPUTexUpload> pending_texture_uploads_;
  // TODO: remove
  size_t tex_upload_i_{};
  std::string get_next_tex_upload_name();

  std::vector<rhi::SamplerHandleHolder> samplers_;

  rhi::TextureHandleHolder default_white_tex_;
  std::vector<uint32_t> indirect_cmd_buf_ids_;
  glm::mat4 get_proj_matrix(float fov = k_default_fov_deg);

  static constexpr float k_z_near = 0.01f;
  static constexpr float k_z_far = 30000.f;
  static constexpr float k_default_fov_deg = 70.0f;

  IdxOffset frame_globals_buf_info_;

  void ensure_per_view_readback_buffers();
  std::vector<std::array<rhi::BufferHandleHolder, k_max_frames_in_flight>>
      meshlet_draw_stats_readback_;
  std::vector<std::array<rhi::BufferHandleHolder, k_max_frames_in_flight>>
      draw_cmd_counts_readback_;

  rhi::QueryPoolHandleHolder query_pools_[k_max_frames_in_flight];
  rhi::QueryPoolHandle get_query_pool() { return query_pools_[curr_frame_idx_].handle; }
  constexpr static int k_query_count = 2;

  rhi::BufferHandleHolder query_resolve_bufs_[k_max_frames_in_flight];
  rhi::Swapchain* swapchain_{};

  // unique to avoid including headers
  std::unique_ptr<gfx::GBufferRenderer> gbuffer_renderer_;
  std::unique_ptr<gfx::CSMRenderer> csm_renderer_;

  int debug_cascade_level_{0};
  rhi::TextureHandle csm_debug_depth_tex_;
  std::array<rhi::TextureViewHandle, CSM_MAX_CASCADES> csm_debug_depth_views_{-1, -1, -1, -1};
  bool reverse_z_{true};

  std::unique_ptr<ModelGPUMgr> model_gpu_mgr_;
};

}  // namespace gfx

}  // namespace TENG_NAMESPACE
