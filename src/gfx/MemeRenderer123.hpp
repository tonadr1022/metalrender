#pragma once

#include <filesystem>
#include <span>

#include "ImGuiRenderer.hpp"
#include "core/Math.hpp"  // IWYU pragma: keep
#include "gfx/BackedGPUAllocator.hpp"
#include "gfx/Config.hpp"
#include "gfx/Device.hpp"
#include "gfx/GFXTypes.hpp"
#include "gfx/ModelInstance.hpp"
#include "gfx/ModelLoader.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/RendererTypes.hpp"
#include "hlsl/shared_instance_data.h"
#include "hlsl/shared_mesh_data.h"
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
  bool draw_imgui;
};

class ScratchBufferPool {
 public:
  explicit ScratchBufferPool(rhi::Device* device) : device_(device) {}
  void reset(size_t frame_idx);
  rhi::BufferHandle alloc(size_t size);

 private:
  struct PerFrame {
    std::vector<rhi::BufferHandleHolder> entries;
    std::vector<rhi::BufferHandleHolder> in_use_entries;
  };
  PerFrame frames_[k_max_frames_in_flight];
  size_t frame_idx_{};

  rhi::Device* device_;
};

enum class DrawBatchType {
  Static,
};

struct DrawBatch {
  struct CreateInfo {
    uint32_t initial_vertex_capacity;
    uint32_t initial_index_capacity;
    uint32_t initial_meshlet_capacity;
    uint32_t initial_mesh_capacity;
    uint32_t initial_meshlet_triangle_capacity;
    uint32_t initial_meshlet_vertex_capacity;
  };

  struct Stats {
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t meshlet_count;
    uint32_t meshlet_triangle_count;
    uint32_t meshlet_vertex_count;
  };

  [[nodiscard]] Stats get_stats() const;

  DrawBatch(DrawBatchType type, rhi::Device& device, const CreateInfo& cinfo);

  struct Alloc {
    OffsetAllocator::Allocation vertex_alloc;
    OffsetAllocator::Allocation index_alloc;
    OffsetAllocator::Allocation meshlet_alloc;
    OffsetAllocator::Allocation mesh_alloc;
    OffsetAllocator::Allocation meshlet_triangles_alloc;
    OffsetAllocator::Allocation meshlet_vertices_alloc;
  };

  void free(const Alloc& alloc) {
    // if (alloc.mesh_alloc.offset != OffsetAllocator::Allocation::NO_SPACE) {
    //   mesh_buf.free(alloc.mesh_alloc);
    // }
    if (alloc.meshlet_alloc.offset != OffsetAllocator::Allocation::NO_SPACE) {
      meshlet_buf.free(alloc.meshlet_alloc);
    }
    if (alloc.vertex_alloc.offset != OffsetAllocator::Allocation::NO_SPACE) {
      vertex_buf.free(alloc.vertex_alloc);
    }
    if (alloc.index_alloc.offset != OffsetAllocator::Allocation::NO_SPACE) {
      index_buf.free(alloc.index_alloc);
    }
    if (alloc.meshlet_triangles_alloc.offset != OffsetAllocator::Allocation::NO_SPACE) {
      meshlet_triangles_buf.free(alloc.meshlet_triangles_alloc);
    }
    if (alloc.meshlet_vertices_alloc.offset != OffsetAllocator::Allocation::NO_SPACE) {
      meshlet_vertices_buf.free(alloc.meshlet_vertices_alloc);
    }
  }

  BackedGPUAllocator vertex_buf;
  BackedGPUAllocator index_buf;
  BackedGPUAllocator meshlet_buf;
  BackedGPUAllocator mesh_buf;
  BackedGPUAllocator meshlet_triangles_buf;
  BackedGPUAllocator meshlet_vertices_buf;
  std::array<rhi::BufferHandleHolder, k_max_frames_in_flight> task_cmd_bufs_;
  std::array<rhi::BufferHandleHolder, k_max_frames_in_flight> out_draw_count_bufs_;
  const DrawBatchType type;
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
  };
  Totals totals{};
};

struct ModelInstanceGPUResources {
  OffsetAllocator::Allocation instance_data_gpu_alloc;
  OffsetAllocator::Allocation meshlet_vis_buf_alloc;
};

class InstanceDataMgr {
 public:
  void init(size_t initial_element_cap, rhi::Device* device) {
    allocator_.emplace(initial_element_cap);
    device_ = device;
    allocate_buffers(initial_element_cap);
  }
  OffsetAllocator::Allocation allocate(size_t element_count);

  [[nodiscard]] size_t allocation_size(OffsetAllocator::Allocation alloc) const {
    return allocator_->allocationSize(alloc);
  }

  void free(OffsetAllocator::Allocation alloc);
  [[nodiscard]] uint32_t max_seen_size() const { return max_seen_size_; }
  [[nodiscard]] rhi::BufferHandle get_instance_data_buf() const {
    return instance_data_buf_.handle;
  }
  [[nodiscard]] rhi::BufferHandle get_draw_cmd_buf() const { return draw_cmd_buf_.handle; }

 private:
  void allocate_buffers(size_t element_count);
  std::optional<OffsetAllocator::Allocator> allocator_;
  rhi::BufferHandleHolder instance_data_buf_;
  rhi::BufferHandleHolder draw_cmd_buf_;
  uint32_t max_seen_size_{};
  uint32_t curr_element_count_{};
  rhi::Device* device_{};
};

class MemeRenderer123 {
 public:
  struct CreateInfo {
    rhi::Device* device;
    Window* window;
    std::filesystem::path resource_dir;
  };
  void init(const CreateInfo& cinfo);
  void render(const RenderArgs& args);
  void on_imgui();
  bool load_model(const std::filesystem::path& path, const glm::mat4& root_transform,
                  ModelInstance& model, ModelGPUHandle& out_handle);
  [[nodiscard]] ModelInstanceGPUHandle add_model_instance(const ModelInstance& model,
                                                          ModelGPUHandle model_gpu_handle);
  ScratchBufferPool& get_scratch_buffer_pool() { return scratch_buffer_pool_.value(); }
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

  rhi::Device* device_{};
  Window* window_{};
  rhi::PipelineHandleHolder test2_pso_;
  rhi::PipelineHandleHolder test_mesh_pso_;
  rhi::PipelineHandleHolder test_task_pso_;
  rhi::PipelineHandleHolder draw_cull_pso_;
  rhi::PipelineHandleHolder test_clear_buf_pso_;
  std::optional<BackedGPUAllocator> materials_buf_;

  rhi::BufferHandleHolder tmp_out_draw_cnt_buf_;
  rhi::BufferHandleHolder tmp_test_buf_;

  size_t frame_num_{};
  size_t curr_frame_idx_{};
  std::filesystem::path resource_dir_;
  std::optional<ScratchBufferPool> scratch_buffer_pool_;
  InstanceDataMgr instance_data_mgr_;
  std::optional<DrawBatch> static_draw_batch_;

  BlockPool<ModelGPUHandle, ModelGPUResources> model_gpu_resource_pool_{20, 1, true};
  BlockPool<ModelInstanceGPUHandle, ModelInstanceGPUResources> model_instance_gpu_resource_pool_{
      1000, 5, true};

  gfx::RenderGraph rg_;

  std::optional<BackedGPUAllocator> meshlet_vis_buf_;
  bool meshlet_vis_buf_dirty_{};

  struct GPUTexUpload {
    std::unique_ptr<void, UntypedDeleterFuncPtr> data;
    rhi::TextureHandle tex;
    uint32_t bytes_per_row;
  };
  std::optional<ImGuiRenderer> imgui_renderer_;

  std::vector<GPUTexUpload> pending_texture_uploads_;

  std::vector<rhi::SamplerHandleHolder> samplers_;

  rhi::TextureHandleHolder default_white_tex_;
  std::vector<uint32_t> indirect_cmd_buf_ids_;
  glm::mat4 get_vp_matrix(const RenderArgs& args);
  // struct TaskCmd {
  //   uint32_t task_cmd_idx;
  //   uint32_t instance_data_idx;
  //   uint32_t num_meshlets;
  // };
  // std::vector<TaskCmd> cmds_;
  std::vector<MeshData> mesh_datas_;
  size_t k_{};
};

}  // namespace gfx
