#pragma once

#include <Metal/MTLBuffer.hpp>
#include <filesystem>

#include "Foundation/NSSharedPtr.hpp"
#include "ModelLoader.hpp"

class MetalDevice;
class WindowApple;

namespace NS {

class AutoreleasePool;

}

namespace MTL {

class CommandQueue;
class Device;
class Function;
class ArgumentEncoder;
class RenderPipelineState;

}  // namespace MTL

struct Shader {
  MTL::Function* vert_func{};
  MTL::Function* frag_func{};
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
  void render();
  void load_model(const std::filesystem::path& path);

 private:
  std::optional<Shader> load_shader();
  std::vector<Model> models_;

  [[maybe_unused]] MetalDevice* device_{};
  WindowApple* window_{};
  MTL::Texture* depth_tex_{};
  MTL::Device* raw_device_{};
  MTL::CommandQueue* main_cmd_queue_{};
  MTL::RenderPipelineState* main_pso_{};

  NS::SharedPtr<MTL::Buffer> main_vert_buffer_{};
  NS::SharedPtr<MTL::Buffer> main_index_buffer_{};
  NS::SharedPtr<MTL::Buffer> main_uniform_buffer_{};
  NS::SharedPtr<MTL::Buffer> materials_buffer_{};
  NS::SharedPtr<MTL::Buffer> scene_arg_buffer_{};
  std::vector<TextureUpload> pending_texture_uploads_;
  constexpr static int k_max_textures{1024};
  void flush_pending_texture_uploads();

  std::filesystem::path shader_dir_{};
  Shader forward_pass_shader_{};
  size_t curr_frame_{};
  size_t frames_in_flight_{2};
};
