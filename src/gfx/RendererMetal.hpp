#pragma once

#include <Metal/MTLBuffer.hpp>
#include <Metal/MTLIndirectCommandBuffer.hpp>
#include <filesystem>
#include <glm/mat4x4.hpp>

#include "Foundation/NSSharedPtr.hpp"
#include "GFXTypes.hpp"
#include "ModelLoader.hpp"
#include "RendererTypes.hpp"
#include "core/Allocator.hpp"
#include "core/Handle.hpp"
#include "core/Pool.hpp"
#include "mesh_shared.h"
#include "metal/BackedGPUAllocator.hpp"
#include "metal/MetalDevice.hpp"
#include "offsetAllocator.hpp"
#include "shader_constants.h"

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

  DrawBatch(DrawBatchType type, rhi::Device& device, const CreateInfo& cinfo);

  struct Stats {
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t meshlet_count;
    uint32_t meshlet_triangle_count;
    uint32_t meshlet_vertex_count;
  };
  [[nodiscard]] Stats get_stats() const;

  BackedGPUAllocator vertex_buf;
  BackedGPUAllocator index_buf;
  BackedGPUAllocator meshlet_buf;
  BackedGPUAllocator mesh_buf;
  BackedGPUAllocator meshlet_triangles_buf;
  BackedGPUAllocator meshlet_vertices_buf;
  const DrawBatchType type;
};

struct DrawBatchAlloc {
  OffsetAllocator::Allocation vertex_alloc;
  OffsetAllocator::Allocation index_alloc;
  OffsetAllocator::Allocation meshlet_alloc;
  OffsetAllocator::Allocation mesh_alloc;
  OffsetAllocator::Allocation meshlet_triangles_alloc;
  OffsetAllocator::Allocation meshlet_vertices_alloc;
};

struct ModelGPUResources {
  OffsetAllocator::Allocation material_alloc;
  // TODO: class lmao
  std::vector<MTL::Texture*> textures;
  DrawBatchAlloc static_draw_batch_alloc;
  std::vector<InstanceData> base_instance_datas;
};

struct ModelInstanceGPUResources {
  OffsetAllocator::Allocation instance_data_gpu_alloc;
};

class InstanceDataMgr {
 public:
  InstanceDataMgr(size_t initial_element_cap, MTL::Device* device);
  [[nodiscard]] MTL::Buffer* model_matrix_buf() const { return model_matrix_buf_.get(); }
  [[nodiscard]] MTL::Buffer* instance_data_buf() const { return instance_data_buf_.get(); }
  OffsetAllocator::Allocation allocate(size_t element_count);
  [[nodiscard]] size_t allocation_size(OffsetAllocator::Allocation alloc) const {
    return allocator_.allocationSize(alloc);
  }
  void free(OffsetAllocator::Allocation alloc) { allocator_.free(alloc); }
  [[nodiscard]] uint32_t max_seen_size() const { return max_seen_size_; }

 private:
  void allocate_buffers(size_t element_count);
  OffsetAllocator::Allocator allocator_;
  NS::SharedPtr<MTL::Buffer> model_matrix_buf_;
  NS::SharedPtr<MTL::Buffer> instance_data_buf_;
  uint32_t max_seen_size_{};
  MTL::Device* device_;
};

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

  bool load_model(const std::filesystem::path& path, const glm::mat4& root_transform,
                  ModelInstance& model, ModelGPUHandle& out_handle);

  [[nodiscard]] ModelInstanceGPUHandle add_model_instance(const ModelInstance& model,
                                                          ModelGPUHandle model_gpu_handle);

  struct AllModelData {
    uint32_t max_objects;
  };
  AllModelData all_model_data_{};

  void free_model(ModelGPUHandle model);
  void on_imgui();
  TextureWithIdx load_material_image(const rhi::TextureDesc& desc);

 private:
  struct PerFrameData {
    NS::SharedPtr<MTL::Buffer> uniform_buf;
  };
  void load_shaders();
  void init_imgui();
  void shutdown_imgui();
  void render_imgui();
  void set_global_uniform_data(const RenderArgs& render_args);
  void flush_pending_texture_uploads();
  void recreate_render_target_textures();
  PerFrameData& get_curr_frame_data() { return per_frame_datas_[curr_frame_ % frames_in_flight_]; }
  NS::SharedPtr<MTL::Buffer> create_buffer(
      size_t size, void* data, MTL::ResourceOptions options = MTL::ResourceStorageModeShared);

  // TODO: make publically reservable
  Pool<ModelGPUHandle, ModelGPUResources> model_gpu_resource_pool_{20};
  Pool<ModelInstanceGPUHandle, ModelInstanceGPUResources> model_instance_gpu_resource_pool_{100};

  std::function<void()> render_imgui_callback_;

  [[maybe_unused]] MetalDevice* device_{};
  WindowApple* window_{};
  MTL::Texture* depth_tex_{};
  MTL::Device* raw_device_{};
  MTL::CommandQueue* main_cmd_queue_{};
  // MTL::RenderPipelineState* main_pso_{};
  MTL::RenderPipelineState* mesh_pso_{};
  MTL::ComputePipelineState* dispatch_mesh_pso_{};

  DrawBatchAlloc upload_geometry(DrawBatchType type, const std::vector<DefaultVertex>& vertices,
                                 const std::vector<rhi::DefaultIndexT>& indices,
                                 const MeshletProcessResult& meshlets);

  std::optional<DrawBatch> static_draw_batch_;

  std::optional<BackedGPUAllocator> materials_buf_;
  MTL::Buffer* all_buffers_buf_{};
  NS::SharedPtr<MTL::Buffer> scene_arg_buffer_;

  std::optional<InstanceDataMgr> instance_data_mgr_;

  NS::SharedPtr<MTL::IndirectCommandBuffer> ind_cmd_buf_;

  NS::SharedPtr<MTL::Buffer> dispatch_mesh_encode_arg_buf_;
  NS::SharedPtr<MTL::Buffer> dispatch_mesh_icb_container_buf_;

  MTL::ArgumentEncoder* global_arg_enc_{};
  std::vector<TextureUpload> pending_texture_uploads_;
  std::vector<MTL::Texture*> all_textures_;
  IndexAllocator texture_index_allocator_{k_max_textures};

  std::filesystem::path shader_dir_;
  std::filesystem::path resource_dir_;
  Shader forward_pass_shader_;
  Shader forward_mesh_shader_;
  Shader dispatch_mesh_shader_;
  size_t curr_frame_;
  size_t frames_in_flight_{2};

  std::vector<PerFrameData> per_frame_datas_;

  MTL::Buffer* get_mtl_buf(BackedGPUAllocator& allocator) {
    return reinterpret_cast<MetalBuffer*>(allocator.get_buffer())->buffer();
  }
};
