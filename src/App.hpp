#pragma once
#include <filesystem>

#include "Camera.hpp"
#include "WindowApple.hpp"
#include "gfx/RendererMetal.hpp"
#include "gfx/ResourceManager.hpp"
#include "gfx/metal/MetalDevice.hpp"
#include "voxels/VoxelRenderer.hpp"
#include "voxels/VoxelWorld.hpp"

struct App {
  App();

  App(const App &) = delete;
  App(App &&) = delete;
  App &operator=(const App &) = delete;
  App &operator=(App &&) = delete;
  ~App() = default;
  void run();
  void on_curse_pos_event(double xpos, double ypos);
  void on_key_event(int key, int action, int mods);

 private:
  void on_hide_mouse_change();
  void load_config();
  void on_imgui();
  void load_model(const std::filesystem::path &path, const glm::mat4 &transform = glm::mat4{1});

  struct Config {
    std::filesystem::path initial_model_path;
  };
  Config config_;
  std::filesystem::path resource_dir_;
  std::filesystem::path shader_dir_;
  std::unique_ptr<MetalDevice> device_;
  std::unique_ptr<WindowApple> window_;
  Camera camera_;
  std::vector<ModelHandle> models_;
  bool first_mouse_{true};
  bool hide_mouse_{false};
  bool imgui_enabled_{true};

  glm::vec2 last_pos_{};
  // TODO: ptr for impl/RHI
  RendererMetal renderer_;
  std::unique_ptr<vox::Renderer> voxel_renderer_;
  std::unique_ptr<vox::World> voxel_world_;
};
