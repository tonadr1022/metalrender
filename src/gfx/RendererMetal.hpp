#pragma once

#include <Metal/MTLBuffer.hpp>
#include <Metal/MTLIndirectCommandBuffer.hpp>
#include <filesystem>
#include <glm/mat4x4.hpp>

#include "Config.hpp"
#include "Foundation/NSSharedPtr.hpp"
#include "GFXTypes.hpp"
#include "ModelLoader.hpp"
#include "RendererTypes.hpp"
#include "core/Allocator.hpp"
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

namespace NS {

class AutoreleasePool;

}

namespace MTL {

class ComputePipelineState;
class IndirectCommandBuffer;
class CommandQueue;
class Device;
class Function;
class ArgumentEncoder;
class RenderPipelineState;

}  // namespace MTL

struct Shader {
  MTL::Function* object_func{};
  MTL::Function* mesh_func{};
  MTL::Function* vert_func{};
  MTL::Function* frag_func{};
  MTL::Function* compute_func{};
};

struct TextureWithIdx {
  MTL::Texture* tex;
  uint32_t idx;
};

struct RenderArgs {
  glm::mat4 view_mat;
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
};

struct ModelInstanceGPUResources {
  OffsetAllocator::Allocation instance_data_gpu_alloc;
};

class InstanceDataMgr {
 public:
  InstanceDataMgr(size_t initial_element_cap, MTL::Device* raw_device, rhi::Device* device);
  [[nodiscard]] MTL::Buffer* model_matrix_buf() const { return model_matrix_buf_.get(); }
  [[nodiscard]] MTL::Buffer* instance_data_buf() const {
    return reinterpret_cast<MetalBuffer*>(device_->get_buf(instance_data_buf_))->buffer();
  }
  OffsetAllocator::Allocation allocate(size_t element_count);
  [[nodiscard]] size_t allocation_size(OffsetAllocator::Allocation alloc) const {
    return allocator_.allocationSize(alloc);
  }

  void free(OffsetAllocator::Allocation alloc) {
    memset(reinterpret_cast<InstanceData*>(instance_data_buf()->contents()) + alloc.offset,
           UINT32_MAX, allocator_.allocationSize(alloc) * sizeof(InstanceData));
    allocator_.free(alloc);
  }
  [[nodiscard]] uint32_t max_seen_size() const { return max_seen_size_; }

 private:
  void allocate_buffers(size_t element_count);
  OffsetAllocator::Allocator allocator_;
  NS::SharedPtr<MTL::Buffer> model_matrix_buf_;
  rhi::BufferHandleHolder instance_data_buf_;
  uint32_t max_seen_size_{};
  rhi::Device* device_{};
  MTL::Device* raw_device_{};
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
  void init_imgui();
  void shutdown_imgui();
  void render_imgui();
  [[nodiscard]] Uniforms set_cpu_global_uniform_data(const RenderArgs& render_args) const;
  [[nodiscard]] CullData set_cpu_cull_data(const Uniforms& uniforms) const;
  void flush_pending_texture_uploads();
  void recreate_render_target_textures();
  // PerFrameData& get_curr_frame_data() { return per_frame_datas_[curr_frame_ % frames_in_flight_];
  // }
  NS::SharedPtr<MTL::Buffer> create_buffer(
      size_t size, void* data, MTL::ResourceOptions options = MTL::ResourceStorageModeShared);

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
  rhi::TextureHandleHolder depth_tex_;
  MTL::Device* raw_device_{};
  MTL::CommandQueue* main_cmd_queue_{};
  // MTL::RenderPipelineState* main_pso_{};
  MTL::RenderPipelineState* mesh_pso_{};
  MTL::ComputePipelineState* dispatch_mesh_pso_{};

  DrawBatch::Alloc upload_geometry(DrawBatchType type, const std::vector<DefaultVertex>& vertices,
                                   const std::vector<rhi::DefaultIndexT>& indices,
                                   const MeshletProcessResult& meshlets);

  std::optional<DrawBatch> static_draw_batch_;

  std::optional<BackedGPUAllocator> materials_buf_;
  NS::SharedPtr<MTL::Buffer> scene_arg_buffer_;

  std::optional<InstanceDataMgr> instance_data_mgr_;
  std::optional<GPUFrameAllocator> gpu_frame_allocator_;
  std::optional<PerFrameBuffer<Uniforms>> gpu_uniform_buf_;
  std::optional<PerFrameBuffer<CullData>> cull_data_buf_;
  bool meshlet_frustum_cull_{true};

  NS::SharedPtr<MTL::IndirectCommandBuffer> ind_cmd_buf_;

  NS::SharedPtr<MTL::Buffer> dispatch_mesh_encode_arg_buf_;
  NS::SharedPtr<MTL::Buffer> dispatch_mesh_icb_container_buf_;

  MTL::ArgumentEncoder* global_arg_enc_{};
  std::vector<TextureUpload> pending_texture_uploads_;
  std::vector<MTL::Texture*> all_textures_;

  std::filesystem::path shader_dir_;
  std::filesystem::path resource_dir_;
  Shader forward_pass_shader_;
  Shader forward_mesh_shader_;
  Shader dispatch_mesh_shader_;
  size_t curr_frame_;
  size_t frames_in_flight_{2};

  // std::vector<PerFrameData> per_frame_datas_;

  MTL::Buffer* get_mtl_buf(BackedGPUAllocator& allocator) {
    return reinterpret_cast<MetalBuffer*>(allocator.get_buffer())->buffer();
  }
};
