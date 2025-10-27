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
  size_t frame_num_{};
};
