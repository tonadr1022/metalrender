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

struct ImDrawData;
class Window;

namespace rhi {
class CmdEncoder;
}

namespace gfx {

struct RenderArgs {
  glm::mat4 view_mat;
  glm::vec3 camera_pos;
  glm::vec4 clear_color;
  bool draw_imgui;
};

struct ModelGPUResources {
  OffsetAllocator::Allocation material_alloc;
  DrawBatch::Alloc static_draw_batch_alloc;
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

struct ModelInstanceGPUResources {
  OffsetAllocator::Allocation instance_data_gpu_alloc;
  OffsetAllocator::Allocation meshlet_vis_buf_alloc;
  ModelGPUHandle model_resources_handle;
};

class InstanceDataMgr {
 public:
  void init(size_t initial_element_cap, rhi::Device* device, BufferCopyMgr* buffer_copy_mgr,
            uint32_t frames_in_flight, bool mesh_shaders_enabled) {
    allocator_.emplace(initial_element_cap);
    device_ = device;
    buffer_copy_mgr_ = buffer_copy_mgr;
    frames_in_flight_ = frames_in_flight;
    mesh_shaders_enabled_ = mesh_shaders_enabled;
    allocate_buffers(initial_element_cap);
  }
  [[nodiscard]] bool has_draws() const { return curr_element_count_ > 0; }
  OffsetAllocator::Allocation allocate(size_t element_count);

  [[nodiscard]] size_t allocation_size(OffsetAllocator::Allocation alloc) const {
    return allocator_->allocationSize(alloc);
  }

  void free(OffsetAllocator::Allocation alloc, uint32_t frame_in_flight);
  void flush_pending_frees(uint32_t curr_frame_in_flight, rhi::CmdEncoder* enc);
  [[nodiscard]] bool has_pending_frees(uint32_t curr_frame_in_flight) const;
  void zero_out_freed_instances(rhi::CmdEncoder* enc);
  [[nodiscard]] uint32_t max_seen_size() const { return max_seen_size_; }
  [[nodiscard]] rhi::BufferHandle get_instance_data_buf() const {
    return instance_data_buf_.handle;
  }
  std::array<std::vector<OffsetAllocator::Allocation>, k_max_frames_in_flight> pending_frees_;
  [[nodiscard]] rhi::BufferHandle get_draw_cmd_buf() const { return draw_cmd_buf_.handle; }

 private:
  void allocate_buffers(size_t element_count);
  std::optional<OffsetAllocator::Allocator> allocator_;
  rhi::BufferHandleHolder instance_data_buf_;
  rhi::BufferHandleHolder draw_cmd_buf_;
  BufferCopyMgr* buffer_copy_mgr_{};
  uint32_t max_seen_size_{};
  uint32_t curr_element_count_{};
  uint32_t frames_in_flight_{};
  rhi::Device* device_{};
  bool mesh_shaders_enabled_{};
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
    Window* window;
    std::filesystem::path resource_dir;
    std::filesystem::path config_file_path;
  };
  explicit MemeRenderer123(const CreateInfo& cinfo);
  ~MemeRenderer123();

  MemeRenderer123(const MemeRenderer123&) = delete;
  MemeRenderer123(MemeRenderer123&&) = delete;
  MemeRenderer123& operator=(const MemeRenderer123&) = delete;
  MemeRenderer123& operator=(MemeRenderer123&&) = delete;

  void render(const RenderArgs& args);
  bool begin_frame();
  void on_imgui();
  bool on_key_event(int key, int action, int mods);
  bool load_model(const std::filesystem::path& path, const glm::mat4& root_transform,
                  ModelInstance& model, ModelGPUHandle& out_handle);
  [[nodiscard]] ModelInstanceGPUHandle add_model_instance(const ModelInstance& model,
                                                          ModelGPUHandle model_gpu_handle);
  void free_instance(ModelInstanceGPUHandle handle);
  void free_model(ModelGPUHandle handle);

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
  DrawBatch::Alloc upload_geometry(DrawBatchType type, const std::vector<DefaultVertex>& vertices,
                                   const std::vector<rhi::DefaultIndexT>& indices,
                                   const MeshletProcessResult& meshlets, std::span<Mesh> meshes);
  struct AllModelData {
    uint32_t max_objects;
    uint32_t max_meshlets;
  };

  AllModelData all_model_data_{};

  struct FinalDrawResults {
    uint32_t drawn_meshlets;
    uint32_t drawn_vertices;
  };

  struct Stats {
    uint32_t total_instance_meshlets{};
    uint32_t total_instance_vertices{};
    uint32_t total_instances{};
    FinalDrawResults draw_results{};
  };
  Stats stats_;

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
  std::optional<BackedGPUAllocator> materials_buf_;
  std::optional<BufferCopyMgr> buffer_copy_mgr_;

  rhi::BufferHandleHolder tmp_out_draw_cnt_buf_;
  rhi::BufferHandleHolder tmp_test_buf_;
  TexAndViewHolder depth_pyramid_tex_;
  void recreate_depth_pyramid_tex();
  void recreate_external_textures();
  int view_mip_{};

  size_t frame_num_{};
  size_t curr_frame_idx_{};
  size_t prev_frame_idx() const {
    return curr_frame_idx_ == 0 ? device_->get_info().frames_in_flight - 1 : curr_frame_idx_ - 1;
  }
  size_t get_frames_ago_idx(size_t frames_ago) const {
    if (curr_frame_idx_ >= frames_ago) {
      return curr_frame_idx_ - frames_ago;
    }
    return device_->get_info().frames_in_flight + curr_frame_idx_ - frames_ago;
  }
  std::filesystem::path resource_dir_;
  std::filesystem::path config_file_path_;
  InstanceDataMgr instance_data_mgr_;
  std::optional<DrawBatch> static_draw_batch_;

  BlockPool<ModelGPUHandle, ModelGPUResources> model_gpu_resource_pool_{20, 1, true};
  BlockPool<ModelInstanceGPUHandle, ModelInstanceGPUResources> model_instance_gpu_resource_pool_{
      1000, 5, true};

  gfx::RenderGraph rg_;

  std::optional<BackedGPUAllocator> meshlet_vis_buf_;

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

  // TODO: rename or sum?
  std::optional<GPUFrameAllocator3> uniforms_allocator_;
  std::optional<GPUFrameAllocator3> staging_buffer_allocator_;
  static constexpr float k_z_near = 0.01f;
  static constexpr float k_z_far = 30000.f;
  static constexpr float k_default_fov_deg = 70.0f;

  bool culling_paused_{};
  bool culling_enabled_{true};
  bool meshlet_frustum_culling_enabled_{true};
  bool meshlet_cone_culling_enabled_{true};
  bool meshlet_occlusion_culling_enabled_{true};
  IdxOffset frame_globals_buf_info_;
  IdxOffset frame_cull_data_buf_info_;
  bool reverse_z_{true};
  bool mesh_shaders_enabled_{true};

  enum class DebugRenderMode {
    None = DEBUG_RENDER_MODE_NONE,
    DepthReduceMips = DEBUG_RENDER_MODE_DEPTH_REDUCE_MIPS,
    MeshletColors = DEBUG_RENDER_MODE_MESHLET_COLORS,
    TriangleColors = DEBUG_RENDER_MODE_TRIANGLE_COLORS,
    InstanceColors = DEBUG_RENDER_MODE_INSTANCE_COLORS,
    Count = DEBUG_RENDER_MODE_COUNT,
  };
  DebugRenderMode debug_render_mode_{DebugRenderMode::None};
  struct MeshletDrawStats {
    uint32_t meshlets_drawn_early;
    uint32_t meshlets_drawn_late;
  };
  rhi::QueryPoolHandleHolder query_pools_[k_max_frames_in_flight];
  rhi::QueryPoolHandle get_query_pool() { return query_pools_[curr_frame_idx_].handle; }
  constexpr static int k_query_count = 10;
  uint64_t timestamps[k_query_count];
  float gpu_frame_time_last_ms_{};
  rhi::BufferHandleHolder out_counts_buf_[k_max_frames_in_flight];
  rhi::BufferHandleHolder out_counts_buf_readback_[k_max_frames_in_flight];
  bool rg_verbose_{};
};

}  // namespace gfx
