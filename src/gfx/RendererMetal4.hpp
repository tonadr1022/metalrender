#pragma once

#include <Metal/Metal.hpp>
#include <filesystem>
#include <functional>

#include "core/Math.hpp"  // IWYU pragma: keep
#include "gfx/RendererTypes.hpp"

class MetalDevice;
class WindowApple;

struct RenderArgs {
  glm::mat4 view_mat;
  glm::vec3 camera_pos;
  bool draw_imgui;
};

struct MyMesh {
  rhi::BufferHandleHolder vertex_buf;
  rhi::BufferHandleHolder index_buf;
  size_t material_id;
  size_t vertex_count;
  size_t index_count;
};

class RendererMetal4 {
 public:
  struct CreateInfo {
    MetalDevice* device;
    WindowApple* window;
    std::filesystem::path resource_dir;
    std::function<void()> render_imgui_callback;
  };
  void init(const CreateInfo& cinfo);
  void render(const RenderArgs& args);
  void on_imgui() {}

 private:
  MetalDevice* device_{};
  WindowApple* window_{};
  rhi::PipelineHandleHolder test_pso_;
  rhi::BufferHandleHolder material_buf_;
  rhi::TextureHandleHolder depth_tex_;
  size_t frame_num_{};
  size_t curr_frame_idx_{};
  std::filesystem::path resource_dir_;

  std::vector<MyMesh> meshes_;
  void create_render_target_textures();
};
