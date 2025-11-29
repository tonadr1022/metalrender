#include "App.hpp"

#include <fstream>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/random.hpp>
#include <random>

#include "core/Logger.hpp"
#include "core/Util.hpp"
#include "gfx/Device.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "platform/apple/AppleWindow.hpp"
#include "tracy/Tracy.hpp"

namespace {

std::filesystem::path get_resource_dir() {
  std::filesystem::path curr_path = std::filesystem::current_path();
  while (curr_path.has_parent_path()) {
    if (std::filesystem::exists(curr_path / "resources")) {
      return curr_path / "resources";
    }
    curr_path = curr_path.parent_path();
  }
  return "";
}

}  // namespace

App::App() {
  ZoneScoped;
  resource_dir_ = get_resource_dir();
  shader_dir_ = resource_dir_ / "shaders";
  load_config();
  device_ = rhi::create_device(rhi::GfxAPI::Metal);
  window_ = std::make_unique<AppleWindow>();
  bool transparent_window = true;
  window_->init([this](int key, int action, int mods) { on_key_event(key, action, mods); },
                [this](double x_pos, double y_pos) { on_curse_pos_event(x_pos, y_pos); },
                transparent_window);
  device_->init({
      .window = window_.get(),
      .shader_lib_dir = resource_dir_ / "shader_out",
      .app_name = "lol",
      .transparent_window = true,
  });

  on_hide_mouse_change();

  ResourceManager::init(ResourceManager::CreateInfo{.renderer = &renderer_});
  renderer_.init(gfx::MemeRenderer123::CreateInfo{
      .device = device_.get(), .window = window_.get(), .resource_dir = resource_dir_});
  // voxel_renderer_ = std::make_unique<vox::Renderer>();
  // voxel_renderer_->init(&renderer_);
  // voxel_world_ = std::make_unique<vox::World>();
  // voxel_world_->init(voxel_renderer_.get(), &renderer_, resource_dir_);
  camera_.pos.x = -5;
}

namespace rando {

namespace {

std::random_device rd{};
std::default_random_engine eng{rd()};

void seed(int seed) { eng.seed(seed); }

float get_float(float min, float max) {
  std::uniform_real_distribution<float> dist{min, max};
  return dist(eng);
}

}  // namespace

}  // namespace rando

void App::run() {
  ZoneScoped;
  int scene = 3;
  if (scene == 0) {
    glm::ivec3 iter{};
    int n = 1;
    glm::ivec3 dims{n, 1, n};
    float dist = 40.0;
    for (iter.z = -dims.z; iter.z <= dims.z; iter.z++) {
      for (iter.x = -dims.x; iter.x <= dims.x; iter.x++) {
        glm::vec3 pos = glm::vec3{iter} * dist;
        load_model(config_.paths[0], glm::translate(glm::mat4{1}, pos));
      }
    }
  } else if (scene == 1) {
    rando::seed(10000000);
    size_t count = 1000;
    float scale = 10;
    float radius = 400;

    for (size_t i = 0; i < count; i++) {
      auto rand_f = [radius]() { return rando::get_float(-radius, radius); };
      auto pos = glm::vec3{rand_f(), rand_f(), rand_f()};
      glm::vec3 randomAxis = glm::linearRand(glm::vec3(-1.0f), glm::vec3(1.0f));
      float randomAngle = glm::linearRand(0.0f, glm::two_pi<float>());
      auto rot = glm::angleAxis(randomAngle, glm::normalize(randomAxis));
      load_model(config_.paths[0], glm::translate(glm::mat4{1}, pos) * glm::mat4_cast(rot) *
                                       glm::scale(glm::mat4{1}, glm::vec3{scale}));
    }
  } else if (scene == 2) {
    glm::vec3 positions[] = {glm::vec3{0, 0, 0}, glm::vec3{-10, 0, 0}};
    float scales[] = {3, 1};
    for (size_t i = 0; i < ARRAY_SIZE(scales); i++) {
      auto p = positions[i];
      load_model(config_.paths[0],
                 glm::translate(glm::mat4{1}, p) * glm::scale(glm::mat4{1}, glm::vec3{scales[i]}));
    }
  } else if (scene == 3) {
    load_model(config_.paths[1], glm::mat4{1});
  }

  // load_model(config_.paths[0], glm::translate(glm::mat4{1}, glm::vec3{0, 0, 0}));
  double last_time = glfwGetTime();
  while (!window_->should_close()) {
    ZoneScopedN("main loop");
    window_->poll_events();

    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    const double curr_time = glfwGetTime();
    auto dt = static_cast<float>(curr_time - last_time);
    last_time = curr_time;
    camera_.update_pos(window_->get_handle(), dt);

    // if (voxel_world_) {
    //   voxel_world_->update(dt, camera_);
    // }

    // for (const auto model : models_) {
    //   ResourceManager::get().get_model(model)->update_transforms();
    // }

    if (imgui_enabled_) {
      on_imgui(dt);
    }

    ImGui::Render();

    const gfx::RenderArgs args{.view_mat = camera_.get_view_mat(),
                               .camera_pos = camera_.pos,
                               .draw_imgui = imgui_enabled_};
    renderer_.render(args);

    ImGui::EndFrame();
  }

  // if (voxel_world_) {
  //   voxel_world_->shutdown();
  // }

  ResourceManager::shutdown();
  window_->shutdown();
  device_->shutdown();
}

void App::on_curse_pos_event(double xpos, double ypos) {
  const glm::vec2 pos = {xpos, ypos};
  if (first_mouse_) {
    first_mouse_ = false;
    last_pos_ = pos;
    return;
  }

  const glm::vec2 offset = {pos.x - last_pos_.x, last_pos_.y - pos.y};
  last_pos_ = pos;
  if (hide_mouse_) {
    camera_.process_mouse(offset);
  }
}

void App::on_key_event(int key, int action, [[maybe_unused]] int mods) {
  const auto is_press = action == GLFW_PRESS;
  if (is_press) {
    if (key == GLFW_KEY_ESCAPE) {
      hide_mouse_ = !hide_mouse_;
      on_hide_mouse_change();
    }
    if (key == GLFW_KEY_TAB) {
      device_->set_vsync(!device_->get_vsync());
    }
    if (key == GLFW_KEY_G && mods & GLFW_MOD_ALT) {
      imgui_enabled_ = !imgui_enabled_;
    }
    if (key == GLFW_KEY_C) {
      if (!models_.empty()) {
        ResourceManager::get().free_model(models_[0]);
        models_.erase(models_.begin());
      }
    }
  }

  if (action == GLFW_PRESS || action == GLFW_REPEAT) {
    if (key == GLFW_KEY_F11) {
      LINFO("setting to fullscreen: {}", !window_->get_fullscreen());
      window_->set_fullscreen(!window_->get_fullscreen());
    }
  }
}

void App::on_hide_mouse_change() {
  glfwSetInputMode(window_->get_handle(), GLFW_CURSOR,
                   hide_mouse_ ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void App::load_config() {
  const std::filesystem::path config_file{resource_dir_ / "config.txt"};
  std::ifstream f(config_file);
  if (!f.is_open()) {
    LCRITICAL("Failed to load config file: {}", config_file.string());
    return;
  }

  std::string path;
  while (f >> path) {
    config_.paths.emplace_back(path);
  }
}

void App::on_imgui(float dt) {
  ImGui::Begin("Renderer");
  ImGui::Text("Frame time %f (ms)", dt * 1000);
  renderer_.on_imgui();
  if (ImGui::TreeNodeEx("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("Position: %f %f %f", camera_.pos.x, camera_.pos.y, camera_.pos.z);
    ImGui::TreePop();
  }

  // if (voxel_world_) {
  //   voxel_world_->on_imgui();
  // }
  ImGui::End();

  ImGui::ShowDemoWindow();
}

void App::load_model(const std::filesystem::path& path, const glm::mat4& transform) {
  auto full_path =
      path.string().contains("Models") ? resource_dir_ / "models" / "gltf" / path : path;
  models_.push_back(ResourceManager::get().load_model(full_path, transform));
}
