#pragma once

#include <filesystem>
#include <span>

#include "ImGuiRenderer.hpp"
#include "core/Config.hpp"
#include "core/Math.hpp"  // IWYU pragma: keep
#include "gfx/BackedGPUAllocator.hpp"
#include "gfx/DrawBatch.hpp"
#include "gfx/GPUFrameAllocator2.hpp"
#include "gfx/ModelInstance.hpp"
#include "gfx/ModelLoader.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/RendererTypes.hpp"
#include "gfx/ShaderManager.hpp"
#include "gfx/renderer/BufferResize.hpp"
#include "gfx/renderer/InstanceMgr.hpp"
#include "gfx/renderer/RenderView.hpp"
#include "gfx/renderer/RendererSettings.hpp"
#include "gfx/rhi/Config.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "hlsl/shared_globals.h"
#include "hlsl/shared_instance_data.h"
#include "offsetAllocator.hpp"

namespace TENG_NAMESPACE {

struct ImDrawData;
class Window;

namespace rhi {
class CmdEncoder;
}

namespace gfx {

class GBufferRenderer;

struct ModelGPUResources {
  OffsetAllocator::Allocation material_alloc;
  GeometryBatch::Alloc static_draw_batch_alloc;
  std::vector<rhi::TextureHandleHolder> textures;
  std::vector<InstanceData> base_instance_datas;
  std::vector<Mesh> meshes;
  std::vector<uint32_t> instance_id_to_node;
  struct Totals {
    uint32_t meshlets;
    uint32_t vertices;
    uint32_t instance_vertices;
    uint32_t instance_meshlets;
    uint32_t task_cmd_count;
  };
  Totals totals{};
};

class MemeRenderer123;

struct ModelInstanceGPUResources {
  InstanceMgr::Alloc instance_data_gpu_alloc;
  ModelGPUHandle model_resources_handle;
};

class MemeRenderer123 {
 public:
  struct CreateInfo {
    rhi::Device* device;
    rhi::Swapchain* swapchain;
    Window* window;
    std::filesystem::path resource_dir;
    std::filesystem::path config_file_path;
    bool mesh_shaders_enabled{true};
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
  void meshlet_stats_imgui(size_t total_scene_models);
  bool on_key_event(int key, int action, int mods);
  bool load_model(const std::filesystem::path& path, const glm::mat4& root_transform,
                  ModelInstance& model, ModelGPUHandle& out_handle);
  void reserve_space_for(std::span<std::pair<ModelGPUHandle, uint32_t>> models);
  [[nodiscard]] ModelInstanceGPUHandle add_model_instance(ModelInstance& model,
                                                          ModelGPUHandle model_gpu_handle);
  void update_model_instance_transforms(const ModelInstance& model);
  void free_instance(ModelInstanceGPUHandle handle);
  void free_model(ModelGPUHandle handle);
  bool mesh_shaders_enabled() const { return mesh_shaders_enabled_; }
  void set_imgui_enabled(bool enabled) { imgui_enabled_ = enabled; }

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
  std::vector<RenderViewId> free_render_view_ids_;

  // guaranteed to be densely packed
  std::vector<RenderView> render_views_;
  RenderViewId main_render_view_id_{RenderViewId::Invalid};
  constexpr static uint32_t k_max_shadow_cascades = 1;
  gch::small_vector<RenderViewId, k_max_shadow_cascades> shadow_map_render_views_;
  size_t shadow_cascade_count_{1};
  bool shadows_enabled_{false};
  bool get_shadows_enabled() const { return shadows_enabled_; }
  void on_shadows_enabled_change(bool shadows_enabled);

  RGPass* clear_bufs_pass_{};

  void set_cull_data_and_globals(const RenderArgs& args);
  GeometryBatch::Alloc upload_geometry(GeometryBatchType type,
                                       const std::vector<DefaultVertex>& vertices,
                                       const std::vector<rhi::DefaultIndexT>& indices,
                                       const MeshletProcessResult& meshlets,
                                       std::span<Mesh> meshes);
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
  rhi::PipelineHandleHolder csm_no_frag_pso_;

  GPUFrameAllocator3 frame_gpu_upload_allocator_;
  BufferCopyMgr buffer_copy_mgr_;
  BackedGPUAllocator materials_buf_;

  int debug_view_mip_{};

  size_t frame_num_{};
  size_t curr_frame_idx_{};

  std::filesystem::path resource_dir_;
  std::filesystem::path config_file_path_;

  InstanceMgr static_instance_mgr_;
  GeometryBatch static_draw_batch_;

  BlockPool<ModelGPUHandle, ModelGPUResources> model_gpu_resource_pool_{20, 1, true};
  BlockPool<ModelInstanceGPUHandle, ModelInstanceGPUResources> model_instance_gpu_resource_pool_{
      1024, 5, true};

  gfx::RenderGraph rg_;

  std::optional<ImGuiRenderer> imgui_renderer_;

  struct GPUTexUpload {
    TextureUpload upload;
    rhi::TextureHandle tex;
    std::string name;
  };
  std::vector<GPUTexUpload> pending_texture_uploads_;
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

  DebugRenderMode debug_render_mode_{DebugRenderMode::None};

  // TODO: this is instance mgr
  struct MeshletDrawStats {
    uint32_t meshlets_drawn_early;
    uint32_t meshlets_drawn_late;
    uint32_t triangles_drawn_early;
    uint32_t triangles_drawn_late;
  };
  rhi::QueryPoolHandleHolder query_pools_[k_max_frames_in_flight];
  rhi::QueryPoolHandle get_query_pool() { return query_pools_[curr_frame_idx_].handle; }
  constexpr static int k_query_count = 2;

  // meshlet_draw_stats_readback_[frame_in_flight][view_id] — CPU readback; GPU buffer is RG-owned
  std::vector<std::vector<rhi::BufferHandleHolder>> meshlet_draw_stats_readback_;
  rhi::BufferHandleHolder query_resolve_bufs_[k_max_frames_in_flight];
  rhi::Swapchain* swapchain_{};

  bool culling_paused_{};
  bool culling_enabled_{true};
  bool meshlet_frustum_culling_enabled_{true};
  bool meshlet_cone_culling_enabled_{true};
  bool meshlet_occlusion_culling_enabled_{true};
  bool reverse_z_{true};
  bool mesh_shaders_enabled_{true};
  bool imgui_enabled_{true};
  bool object_occlusion_culling_enabled_{true};
  bool rg_verbose_{};

  RendererSettings settings_{};

  // unique to avoid including headers
  std::unique_ptr<gfx::GBufferRenderer> gbuffer_renderer_;
};

}  // namespace gfx

}  // namespace TENG_NAMESPACE
