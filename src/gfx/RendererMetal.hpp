#pragma once

#include <Metal/MTLBuffer.hpp>
#include <Metal/MTLIndirectCommandBuffer.hpp>
#include <filesystem>
#include <glm/mat4x4.hpp>
#include <span>

#include "Config.hpp"
#include "Foundation/NSSharedPtr.hpp"
#include "GFXTypes.hpp"
#include "ModelLoader.hpp"
#include "RendererTypes.hpp"
#include "core/BitUtil.hpp"
#include "core/Handle.hpp"
#include "core/Pool.hpp"
#include "mesh_shared.h"
#include "metal/BackedGPUAllocator.hpp"
#include "metal/MetalDevice.hpp"
#include "offsetAllocator.hpp"
#include "shader_global_uniforms.h"

class MetalDevice;
class WindowApple;

namespace CA {

class MetalDrawable;

}

namespace NS {

class AutoreleasePool;

}

namespace MTL {

class CommandBuffer;
class Library;
class ComputePipelineState;
class IndirectCommandBuffer;
class CommandQueue;
class Device;
class Function;
class ArgumentEncoder;
class RenderPipelineState;

}  // namespace MTL

struct FuncConst {
  std::string name;
  void* val;
  enum class Type {
    Bool,
  };
  Type type;
};

enum ShaderStage {
  ShaderStage_Vertex,
  ShaderStage_Fragment,
  ShaderStage_Object,
  ShaderStage_Mesh,
  ShaderStage_Compute,
  ShaderStage_Count,
};

struct TextureWithIdx {
  MTL::Texture* tex;
  uint32_t idx;
};

struct RenderArgs {
  glm::mat4 view_mat;
  glm::vec3 camera_pos;
  bool draw_imgui;
};

enum class DrawBatchType {
  Static,
};

const char* draw_batch_type_to_string(DrawBatchType type);

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
    if (alloc.mesh_alloc.offset != OffsetAllocator::Allocation::NO_SPACE) {
      mesh_buf.free(alloc.mesh_alloc);
    }
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
  const DrawBatchType type;
};

struct ModelGPUResources {
  OffsetAllocator::Allocation material_alloc;
  // TODO: class lmao
  std::vector<MTL::Texture*> textures;
  DrawBatch::Alloc static_draw_batch_alloc;
  std::vector<InstanceData> base_instance_datas;
  std::vector<uint32_t> instance_id_to_node;
  size_t tot_meshlet_count{};
};

struct ModelInstanceGPUResources {
  OffsetAllocator::Allocation instance_data_gpu_alloc;
  OffsetAllocator::Allocation meshlet_vis_buf_alloc;
};

class InstanceDataMgr {
 public:
  InstanceDataMgr(size_t initial_element_cap, MTL::Device* raw_device, rhi::Device* device);
  [[nodiscard]] MTL::Buffer* instance_data_buf() const {
    return reinterpret_cast<MetalBuffer*>(device_->get_buf(instance_data_buf_))->buffer();
  }
  OffsetAllocator::Allocation allocate(size_t element_count);
  [[nodiscard]] size_t allocation_size(OffsetAllocator::Allocation alloc) const {
    return allocator_.allocationSize(alloc);
  }
  [[nodiscard]] MTL::IndirectCommandBuffer* icb() const { return main_icb_.get(); }

  void free(OffsetAllocator::Allocation alloc) {
    auto element_count = allocator_.allocationSize(alloc);
    memset(reinterpret_cast<InstanceData*>(instance_data_buf()->contents()) + alloc.offset,
           UINT32_MAX, element_count * sizeof(InstanceData));
    allocator_.free(alloc);
    curr_element_count_ -= element_count;
  }
  [[nodiscard]] uint32_t max_seen_size() const { return max_seen_size_; }

 private:
  void allocate_buffers(size_t element_count);
  void resize_icb(size_t element_count);
  OffsetAllocator::Allocator allocator_;
  rhi::BufferHandleHolder instance_data_buf_;
  uint32_t max_seen_size_{};
  uint32_t icb_element_count_{};
  uint32_t curr_element_count_{};
  rhi::Device* device_{};
  MTL::Device* raw_device_{};
  NS::SharedPtr<MTL::IndirectCommandBuffer> main_icb_;
};

class GPUFrameAllocator;

template <typename ElementT>
class PerFrameBuffer {
  friend class GPUFrameAllocator;
  PerFrameBuffer(GPUFrameAllocator& allocator, size_t offset_bytes, size_t element_count)
      : allocator_(allocator), element_count_(element_count), offset_bytes_(offset_bytes) {}

 public:
  [[nodiscard]] rhi::Buffer* get_buf() const;
  [[nodiscard]] size_t get_offset_bytes() const { return offset_bytes_; }
  void fill(const ElementT& data);

 private:
  GPUFrameAllocator& allocator_;
  size_t element_count_{};
  size_t offset_bytes_{};
};

class GPUFrameAllocator {
 public:
  GPUFrameAllocator(rhi::Device* device, size_t size, size_t frames_in_flight);
  void switch_to_next_buffer() { curr_frame_idx_ = (curr_frame_idx_ + 1) % frames_in_flight_; }

  rhi::Buffer* get_buffer() { return device_->get_buf(buffers_[curr_frame_idx_]); }

  template <typename ElementT>
  PerFrameBuffer<ElementT> create_buffer(size_t element_count) {
    auto buf = PerFrameBuffer<ElementT>(*this, curr_alloc_offset_, element_count);
    curr_alloc_offset_ += util::align_256(element_count * sizeof(ElementT));
    return buf;
  }

 private:
  std::array<rhi::BufferHandleHolder, k_max_frames_in_flight> buffers_{};
  size_t curr_alloc_offset_{};
  size_t curr_frame_idx_{};
  size_t frames_in_flight_{};
  rhi::Device* device_{};
};

template <typename ElementT>
rhi::Buffer* PerFrameBuffer<ElementT>::get_buf() const {
  return allocator_.get_buffer();
}
template <typename ElementT>
void PerFrameBuffer<ElementT>::fill(const ElementT& data) {
  for (size_t i = 0; i < element_count_; i++) {
    *(reinterpret_cast<ElementT*>(reinterpret_cast<uint8_t*>(get_buf()->contents()) +
                                  offset_bytes_) +
      i) = data;
  }
}

class RendererMetal {
 public:
  struct CreateInfo {
    MetalDevice* device;
    WindowApple* window;
    std::filesystem::path resource_dir;
    std::function<void()> render_imgui_callback;
  };

  void init(const CreateInfo& cinfo);
  void shutdown();
  void render(const RenderArgs& render_args);
  [[nodiscard]] rhi::Device* get_device() const { return device_; }

  bool load_model(const std::filesystem::path& path, const glm::mat4& root_transform,
                  ModelInstance& model, ModelGPUHandle& out_handle);

  [[nodiscard]] ModelInstanceGPUHandle add_model_instance(const ModelInstance& model,
                                                          ModelGPUHandle model_gpu_handle);

  void free_model(ModelGPUHandle handle);
  void free_instance(ModelInstanceGPUHandle handle);
  void on_imgui();
  TextureWithIdx load_material_image(const rhi::TextureDesc& desc);

 private:
  // struct PerFrameData {
  //   NS::SharedPtr<MTL::Buffer> uniform_buf;
  // };
  void load_shaders();
  void load_pipelines();
  void init_imgui();
  void shutdown_imgui();
  void render_imgui();
  [[nodiscard]] Uniforms set_cpu_global_uniform_data(const RenderArgs& render_args) const;
  [[nodiscard]] CullData set_cpu_cull_data(const Uniforms& uniforms, const RenderArgs& render_args);
  void flush_pending_texture_uploads();
  void recreate_render_target_textures();
  void recreate_depth_pyramid_tex();

  struct AllModelData {
    uint32_t max_objects;
  };
  AllModelData all_model_data_{};

  // TODO: make publically reservable
  Pool<ModelGPUHandle, ModelGPUResources> model_gpu_resource_pool_{20};
  Pool<ModelInstanceGPUHandle, ModelInstanceGPUResources> model_instance_gpu_resource_pool_{100};

  std::function<void()> render_imgui_callback_;

  [[maybe_unused]] MetalDevice* device_{};
  WindowApple* window_{};

  MTL::Device* raw_device_{};
  MTL::CommandQueue* main_cmd_queue_{};
  MTL::RenderPipelineState* mesh_pso_{};
  MTL::RenderPipelineState* mesh_late_pso_{};
  MTL::RenderPipelineState* vertex_pso_{};
  MTL::RenderPipelineState* full_screen_tex_pso_{};
  MTL::ComputePipelineState* dispatch_mesh_pso_{};
  MTL::ComputePipelineState* dispatch_vertex_pso_{};
  MTL::ComputePipelineState* depth_reduce_pso_{};

  rhi::TextureHandleHolder depth_tex_;
  rhi::TextureHandleHolder depth_pyramid_tex_;
  // TODO: refactor
  std::array<NS::SharedPtr<MTL::Texture>, 16> depth_pyramid_tex_views_{};
  static constexpr bool k_reverse_z = true;
  enum class DebugRenderView {
    None,
    DepthPyramidTex,
    Count,
  };
  const char* debug_render_view_to_str(DebugRenderView view) {
    switch (view) {
      case DebugRenderView::DepthPyramidTex:
        return "Depth Pyramid Texture";
      default:
        return "None";
    }
  }

  DebugRenderView debug_render_view_{DebugRenderView::None};
  int debug_depth_pyramid_mip_level_{};

  std::optional<BackedGPUAllocator> meshlet_vis_buf_;
  rhi::TextureHandleHolder default_white_tex_;

  DrawBatch::Alloc upload_geometry(DrawBatchType type, const std::vector<DefaultVertex>& vertices,
                                   const std::vector<rhi::DefaultIndexT>& indices,
                                   const MeshletProcessResult& meshlets, std::span<Mesh> meshes);

  std::optional<DrawBatch> static_draw_batch_;

  std::optional<BackedGPUAllocator> materials_buf_;
  NS::SharedPtr<MTL::Buffer> scene_arg_buffer_;

  std::optional<InstanceDataMgr> instance_data_mgr_;
  std::optional<GPUFrameAllocator> gpu_frame_allocator_;
  std::optional<PerFrameBuffer<Uniforms>> gpu_uniform_buf_;
  std::optional<PerFrameBuffer<CullData>> cull_data_buf_;

  bool meshlet_frustum_cull_{true};
  static constexpr float k_z_near{0.001};
  static constexpr float k_z_far{10'000};

  NS::SharedPtr<MTL::Buffer> dispatch_mesh_encode_arg_buf_;
  MTL::ArgumentEncoder* dispatch_mesh_encode_arg_enc_{};
  NS::SharedPtr<MTL::Buffer> dispatch_vertex_encode_arg_buf_;
  MTL::ArgumentEncoder* dispatch_vertex_encode_arg_enc_{};
  NS::SharedPtr<MTL::Buffer> main_object_arg_buf_;
  MTL::ArgumentEncoder* main_object_arg_enc_{};

  rhi::BufferHandleHolder main_icb_container_buf_;
  MTL::ArgumentEncoder* main_icb_container_arg_enc_{};

  MTL::ArgumentEncoder* global_arg_enc_{};
  std::vector<TextureUpload> pending_texture_uploads_;
  std::vector<rhi::TextureHandleHolder> all_textures_;

  std::filesystem::path shader_dir_;
  std::filesystem::path resource_dir_;
  std::unordered_map<std::string, MTL::Function*> shader_funcs_;

  [[nodiscard]] size_t curr_frame_in_flight() const { return curr_frame_ % frames_in_flight_; }
  size_t curr_frame_;
  size_t frames_in_flight_{2};
  bool meshlet_vis_buf_dirty_{};
  bool meshlet_occlusion_culling_enabled_{true};
  bool culling_paused_{false};
  bool object_frust_cull_enabled_{true};

  // std::vector<PerFrameData> per_frame_datas_;

  MTL::Function* get_function(const char* name, bool load = true);
  MTL::Library* default_shader_lib_;
  MTL::Function* create_function(const std::string& name, const std::string& specialized_name,
                                 std::span<FuncConst> consts);

  MTL::Buffer* get_mtl_buf(BackedGPUAllocator& allocator) {
    return reinterpret_cast<MetalBuffer*>(allocator.get_buffer())->buffer();
  }

  MTL::Buffer* get_mtl_buf(const rhi::BufferHandleHolder& handle) {
    return reinterpret_cast<MetalBuffer*>(device_->get_buf(handle))->buffer();
  }

  MTL::Texture* get_mtl_tex(const rhi::TextureHandleHolder& handle) {
    return reinterpret_cast<MetalTexture*>(device_->get_tex(handle))->texture();
  }

  void encode_regular_frame(const RenderArgs& render_args, MTL::CommandBuffer* buf,
                            const CA::MetalDrawable* drawable);
  void encode_debug_depth_pyramid_view(MTL::CommandBuffer* buf, const CA::MetalDrawable* drawable);
};
