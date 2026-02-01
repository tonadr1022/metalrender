#pragma once

#include <filesystem>
#include <span>

#include "ImGuiRenderer.hpp"
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
#include "gfx/rhi/Config.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "hlsl/shared_globals.h"
#include "hlsl/shared_instance_data.h"
#include "offsetAllocator.hpp"

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

struct ImDrawData;
class Window;

namespace rhi {
class CmdEncoder;
}

namespace gfx {

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

class InstanceMgr {
 public:
  InstanceMgr(const InstanceMgr&) = delete;
  InstanceMgr(InstanceMgr&&) = delete;
  InstanceMgr& operator=(const InstanceMgr&) = delete;
  InstanceMgr& operator=(InstanceMgr&&) = delete;
  struct Alloc {
    OffsetAllocator::Allocation instance_data_alloc;
    OffsetAllocator::Allocation meshlet_vis_alloc;
  };

  InstanceMgr(rhi::Device& device, BufferCopyMgr& buffer_copy_mgr, uint32_t frames_in_flight,
              MemeRenderer123& renderer);
  [[nodiscard]] bool has_draws() const { return curr_element_count_ > 0; }
  Alloc allocate(uint32_t element_count, uint32_t meshlet_instance_count);

  [[nodiscard]] size_t allocation_size(OffsetAllocator::Allocation alloc) const {
    return allocator_.allocationSize(alloc);
  }

  void free(const Alloc& alloc, uint32_t frame_in_flight);
  void flush_pending_frees(uint32_t curr_frame_in_flight, rhi::CmdEncoder* enc);
  [[nodiscard]] bool has_pending_frees(uint32_t curr_frame_in_flight) const;
  void zero_out_freed_instances(rhi::CmdEncoder* enc);
  [[nodiscard]] rhi::BufferHandle get_instance_data_buf() const {
    return instance_data_buf_.handle;
  }
  [[nodiscard]] const BackedGPUAllocator& get_meshlet_vis_buf() const { return meshlet_vis_buf_; }
  std::array<std::vector<Alloc>, k_max_frames_in_flight> pending_frees_;
  [[nodiscard]] rhi::BufferHandle get_draw_cmd_buf() const { return draw_cmd_buf_.handle; }

  struct Stats {
    uint32_t max_instance_data_count;
    uint32_t max_seen_meshlet_instance_count;
  };

  [[nodiscard]] const Stats& stats() const { return stats_; }

 private:
  OffsetAllocator::Allocation allocate_instance_data(uint32_t element_count);
  void ensure_buffer_space(size_t element_count);
  OffsetAllocator::Allocator allocator_;
  rhi::BufferHandleHolder instance_data_buf_;
  rhi::BufferHandleHolder draw_cmd_buf_;
  BackedGPUAllocator meshlet_vis_buf_;
  BufferCopyMgr& buffer_copy_mgr_;
  Stats stats_{};
  uint32_t curr_element_count_{};
  uint32_t frames_in_flight_{};
  rhi::Device& device_;
  MemeRenderer123& renderer_;
};

struct ModelInstanceGPUResources {
  InstanceMgr::Alloc instance_data_gpu_alloc;
  ModelGPUHandle model_resources_handle;
};

struct IdxOffset {
  rhi::BufferHandle buf;
  uint idx;
  uint offset_bytes;
};

struct TexAndViewHolder : public rhi::TextureHandleHolder {
  TexAndViewHolder() = default;
  explicit TexAndViewHolder(rhi::TextureHandleHolder&& handle)
      : rhi::TextureHandleHolder(std::move(handle)) {}
  TexAndViewHolder(const TexAndViewHolder&) = delete;
  TexAndViewHolder(TexAndViewHolder&&) = default;
  TexAndViewHolder& operator=(const TexAndViewHolder&) = delete;
  TexAndViewHolder& operator=(TexAndViewHolder&&) = default;
  ~TexAndViewHolder();
  std::vector<rhi::TextureViewHandle> views;
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
  void on_imgui();
  bool on_key_event(int key, int action, int mods);
  bool load_model(const std::filesystem::path& path, const glm::mat4& root_transform,
                  ModelInstance& model, ModelGPUHandle& out_handle);
  [[nodiscard]] ModelInstanceGPUHandle add_model_instance(const ModelInstance& model,
                                                          ModelGPUHandle model_gpu_handle);
  void free_instance(ModelInstanceGPUHandle handle);
  void free_model(ModelGPUHandle handle);
  bool mesh_shaders_enabled() const { return mesh_shaders_enabled_; }
  void set_imgui_enabled(bool enabled) { imgui_enabled_ = enabled; }

 private:
  void init_imgui();
  void shutdown_imgui();
  void flush_pending_texture_uploads(rhi::CmdEncoder* enc);
  [[nodiscard]] uint32_t get_bindless_idx(const rhi::BufferHandleHolder& buf) const;
  [[nodiscard]] uint32_t get_bindless_idx(const rhi::BufferHandle& buf) const {
    return device_->get_buf(buf)->bindless_idx();
  }
  void add_render_graph_passes(const RenderArgs& args);
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

  Stats stats_{};

  std::unique_ptr<gfx::ShaderManager> shader_mgr_;
  rhi::Device* device_{};
  Window* window_{};
  rhi::PipelineHandleHolder test2_pso_;
  rhi::PipelineHandleHolder test_task_pso_;
  rhi::PipelineHandleHolder draw_cull_pso_;
  rhi::PipelineHandleHolder reset_counts_buf_pso_;
  rhi::PipelineHandleHolder depth_reduce_pso_;
  rhi::PipelineHandleHolder shade_pso_;
  rhi::PipelineHandleHolder tex_only_pso_;

  GPUFrameAllocator3 frame_gpu_upload_allocator_;
  BufferCopyMgr buffer_copy_mgr_;
  BackedGPUAllocator materials_buf_;

  TexAndViewHolder depth_pyramid_tex_;
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
  IdxOffset frame_view_buf_info_;
  IdxOffset frame_cull_data_buf_info_;

  enum class DebugRenderMode {
    None = DEBUG_RENDER_MODE_NONE,
    DepthReduceMips = DEBUG_RENDER_MODE_DEPTH_REDUCE_MIPS,
    MeshletColors = DEBUG_RENDER_MODE_MESHLET_COLORS,
    TriangleColors = DEBUG_RENDER_MODE_TRIANGLE_COLORS,
    InstanceColors = DEBUG_RENDER_MODE_INSTANCE_COLORS,
    Count = DEBUG_RENDER_MODE_COUNT,
  } debug_render_mode_{DebugRenderMode::None};

  // TODO: this is instance mgr
  struct MeshletDrawStats {
    uint32_t meshlets_drawn_early;
    uint32_t meshlets_drawn_late;
  };
  rhi::QueryPoolHandleHolder query_pools_[k_max_frames_in_flight];
  rhi::QueryPoolHandle get_query_pool() { return query_pools_[curr_frame_idx_].handle; }
  constexpr static int k_query_count = 2;
  rhi::BufferHandleHolder out_counts_buf_[k_max_frames_in_flight];
  rhi::BufferHandleHolder out_counts_buf_readback_[k_max_frames_in_flight];
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
  bool rg_verbose_{};
};

}  // namespace gfx

} // namespace TENG_NAMESPACE
