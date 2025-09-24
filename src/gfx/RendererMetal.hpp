#pragma once

#include <Metal/MTLBuffer.hpp>
#include <Metal/MTLIndirectCommandBuffer.hpp>
#include <filesystem>
#include <glm/mat4x4.hpp>

#include "Foundation/NSSharedPtr.hpp"
#include "GFXTypes.hpp"
#include "ModelLoader.hpp"
#include "core/Allocator.hpp"
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
};

class RendererMetal {
 public:
  struct CreateInfo {
    MetalDevice* device;
    WindowApple* window;
    std::filesystem::path resource_dir;
  };

  void init(const CreateInfo& cinfo);
  void shutdown();
  void render(const RenderArgs& render_args);
  void load_model(const std::filesystem::path& path);
  TextureWithIdx load_material_image(const TextureDesc& desc);

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

  std::vector<Model> models_;

  [[maybe_unused]] MetalDevice* device_{};
  WindowApple* window_{};
  MTL::Texture* depth_tex_{};
  MTL::Device* raw_device_{};
  MTL::CommandQueue* main_cmd_queue_{};
  MTL::RenderPipelineState* main_pso_{};
  MTL::RenderPipelineState* mesh_pso_{};
  MTL::ComputePipelineState* dispatch_mesh_pso_{};

  NS::SharedPtr<MTL::Buffer> main_vert_buffer_;
  NS::SharedPtr<MTL::Buffer> main_index_buffer_;
  NS::SharedPtr<MTL::Buffer> materials_buffer_;
  NS::SharedPtr<MTL::Buffer> scene_arg_buffer_;

  IndexAllocator instance_idx_allocator_{1024};
  NS::SharedPtr<MTL::Buffer> instance_model_matrix_buf_;
  NS::SharedPtr<MTL::Buffer> instance_material_id_buf_;
  NS::SharedPtr<MTL::IndirectCommandBuffer> ind_cmd_buf_;

  NS::SharedPtr<MTL::Buffer> instance_data_buf_;
  // NS::SharedPtr<MTL::Buffer> object_shader_param_buf_;
  NS::SharedPtr<MTL::Buffer> meshlet_buf_;
  // NS::SharedPtr<MTL::Buffer> gpu_mesh_data_buf_;
  NS::SharedPtr<MTL::Buffer> meshlet_vertices_buf_;
  NS::SharedPtr<MTL::Buffer> meshlet_triangles_buf_;
  NS::SharedPtr<MTL::Buffer> dispatch_mesh_encode_arg_buf_;
  NS::SharedPtr<MTL::Buffer> dispatch_mesh_icb_container_buf_;

  NS::SharedPtr<MTL::Buffer> obj_info_buf_;
  MTL::ArgumentEncoder* global_arg_enc_{};
  std::vector<TextureUpload> pending_texture_uploads_;
  std::vector<Material> all_materials_;
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
  uint32_t tot_meshes_{0};
  enum class DrawMode {
    IndirectMeshShader,
    MeshShader,
    VertexShader,
  };

  DrawMode draw_mode_{DrawMode::IndirectMeshShader};
};
