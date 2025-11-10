#pragma once

#include <Metal/MTLBuffer.hpp>
#include <Metal/MTLDevice.hpp>
#include <Metal/MTLIndirectCommandBuffer.hpp>
#include <filesystem>
#include <glm/mat4x4.hpp>
#include <span>

#include "GFXTypes.hpp"
#include "ModelLoader.hpp"
#include "RendererTypes.hpp"
#include "core/Handle.hpp"
#include "core/Pool.hpp"
#include "gfx/GPUFrameAllocator.hpp"
#include "mesh_shared.h"
#include "metal/BackedGPUAllocator.hpp"
#include "metal/MetalDevice.hpp"
#include "offsetAllocator.hpp"
#include "shader_global_uniforms.h"
#include "util/Stats.hpp"

class MetalDevice;
class Window;

namespace CA {

class MetalDrawable;

}

namespace NS {

class AutoreleasePool;

}

namespace MTL {

class MeshRenderPipelineDescriptor;
class RenderPipelineDescriptor;
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
  void init(size_t initial_element_cap, MTL::Device* raw_device, rhi::Device* device);
  [[nodiscard]] MTL::Buffer* instance_data_buf() const {
    return reinterpret_cast<MetalBuffer*>(device_->get_buf(instance_data_buf_))->buffer();
  }
  OffsetAllocator::Allocation allocate(size_t element_count);
  [[nodiscard]] size_t allocation_size(OffsetAllocator::Allocation alloc) const {
    return allocator_->allocationSize(alloc);
  }
  [[nodiscard]] MTL::IndirectCommandBuffer* icb() const { return main_icb_; }

  void free(OffsetAllocator::Allocation alloc) {
    auto element_count = allocator_->allocationSize(alloc);
    memset(reinterpret_cast<InstanceData*>(instance_data_buf()->contents()) + alloc.offset,
           UINT32_MAX, element_count * sizeof(InstanceData));
    allocator_->free(alloc);
    curr_element_count_ -= element_count;
  }
  [[nodiscard]] uint32_t max_seen_size() const { return max_seen_size_; }

 private:
  void allocate_buffers(size_t element_count);
  void resize_icb(size_t element_count);
  std::optional<OffsetAllocator::Allocator> allocator_;
  rhi::BufferHandleHolder instance_data_buf_;
  uint32_t max_seen_size_{};
  uint32_t icb_element_count_{};
  uint32_t curr_element_count_{};
  rhi::Device* device_{};
  MTL::Device* raw_device_{};
  MTL::IndirectCommandBuffer* main_icb_ = nullptr;
};

class RendererMetal {
 public:
  struct CreateInfo {
    MetalDevice* device;
    Window* window;
    std::filesystem::path resource_dir;
    std::function<void()> render_imgui_callback;
  };
  using MainRenderPassCallback = std::function<void(
      MTL::RenderCommandEncoder* enc, MTL::Buffer* uniform_buf, const Uniforms& cpu_uniforms)>;

  void add_main_render_pass_callback(MainRenderPassCallback&& cb) {
    main_render_pass_callbacks_.emplace_back(cb);
  }

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
  void load_tex_array(TextureArrayUpload upload) {
    pending_texture_array_uploads_.emplace_back(std::move(upload));
  }
  TextureWithIdx load_material_image(const rhi::TextureDesc& desc);

  enum class RenderMode { Default, Normals, NormalMap, Count };
  const constexpr char* to_string(RendererMetal::RenderMode mode);

  MTL::RenderPipelineState* load_pipeline(MTL::RenderPipelineDescriptor* desc);
  MTL::RenderPipelineState* load_pipeline(MTL::MeshRenderPipelineDescriptor* desc);
  MTL::Function* get_function(const char* name, bool load = true);

  const MTL::PixelFormat main_pixel_format{MTL::PixelFormatBGRA8Unorm};

 private:
  struct GPUTexUpload {
    void* data;
    rhi::TextureHandleHolder tex;
    glm::uvec3 dims;
    uint32_t bytes_per_row;
  };
  // struct PerFrameData {
  //   NS::SharedPtr<MTL::Buffer> uniform_buf;
  // };
  void load_shaders();
  void load_pipelines();
  void init_imgui();
  void shutdown_imgui();
  void render_imgui();
  void set_cpu_global_uniform_data(const RenderArgs& render_args);
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
  Window* window_{};

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
  std::array<MTL::Texture*, 16> depth_pyramid_tex_views_{};
  static constexpr bool k_reverse_z = true;
  enum class DebugRenderView {
    None,
    DepthPyramidTex,
    Count,
  };

  const char* to_string(DebugRenderView view) {
    switch (view) {
      case DebugRenderView::DepthPyramidTex:
        return "Depth Pyramid Texture";
      case DebugRenderView::None:
      default:
        return "None";
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
  MTL::Buffer* scene_arg_buffer_;

  InstanceDataMgr instance_data_mgr_;
  std::optional<GPUFrameAllocator> gpu_frame_allocator_;
  std::optional<PerFrameBuffer<Uniforms>> gpu_uniform_buf_;
  std::optional<PerFrameBuffer<CullData>> cull_data_buf_;

  bool meshlet_frustum_cull_{true};
  static constexpr float k_z_near{0.001};
  static constexpr float k_z_far{10'000};

  MTL::Buffer* dispatch_mesh_encode_arg_buf_;
  MTL::ArgumentEncoder* dispatch_mesh_encode_arg_enc_{};
  MTL::Buffer* dispatch_vertex_encode_arg_buf_;
  MTL::ArgumentEncoder* dispatch_vertex_encode_arg_enc_{};
  MTL::Buffer* main_object_arg_buf_;
  MTL::ArgumentEncoder* main_object_arg_enc_{};
  rhi::BufferHandleHolder final_meshlet_draw_count_buf_;
  rhi::BufferHandleHolder final_meshlet_draw_count_cpu_buf_;

  rhi::BufferHandleHolder main_icb_container_buf_;
  MTL::ArgumentEncoder* main_icb_container_arg_enc_{};

  MTL::ArgumentEncoder* global_arg_enc_{};
  std::vector<GPUTexUpload> pending_texture_uploads_;
  std::vector<TextureArrayUpload> pending_texture_array_uploads_;
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

  RenderMode render_mode_{RenderMode::Default};

  // std::vector<PerFrameData> per_frame_datas_;

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
  MTL::Texture* get_mtl_tex(const rhi::TextureHandle& handle) {
    return reinterpret_cast<MetalTexture*>(device_->get_tex(handle))->texture();
  }

  void encode_regular_frame(const RenderArgs& render_args, MTL::CommandBuffer* buf,
                            const CA::MetalDrawable* drawable);
  void encode_debug_depth_pyramid_view(MTL::CommandBuffer* buf, const CA::MetalDrawable* drawable);

  struct FinalDrawResults {
    uint32_t drawn_meshlets;
    uint32_t drawn_vertices;
  };
  struct Stats {
    uint32_t total_instance_meshlets{};
    uint32_t total_instance_vertices{};
    FinalDrawResults draw_results{};
  };

  Stats stats_;
  util::RollingAvgCtr frame_times_{128};

  std::vector<MainRenderPassCallback> main_render_pass_callbacks_;
  Uniforms cpu_uniforms_;
};

static constexpr const char* to_string(RendererMetal::RenderMode mode);
