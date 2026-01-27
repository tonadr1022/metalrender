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
  std::filesystem::current_path(resource_dir_.parent_path());
  local_resource_dir_ = resource_dir_ / "local";
  if (!std::filesystem::exists(local_resource_dir_)) {
    std::filesystem::create_directories(local_resource_dir_);
  }
  shader_dir_ = resource_dir_ / "shaders";

  camera_path_ = local_resource_dir_ / "camera.txt";
  config_path_ = local_resource_dir_ / "config.txt";

  load_config();
  device_ = rhi::create_device(rhi::GfxAPI::Metal);
  window_ = std::make_unique<AppleWindow>();
  bool transparent_window = true;
  Window::InitInfo win_init_info{
      .key_callback_fn = [this](int key, int action, int mods) { on_key_event(key, action, mods); },
      .cursor_pos_callback_fn = [this](double x_pos,
                                       double y_pos) { on_curse_pos_event(x_pos, y_pos); },
      .transparent_window = transparent_window,
      .win_dims_x = config_.win_dims.x,
      .win_dims_y = config_.win_dims.y,
      .floating_window = false,
  };
  window_->init(win_init_info);
  window_->set_window_position(config_.win_pos);
  device_->init({
      .window = window_.get(),
      .shader_lib_dir = resource_dir_ / "shader_out",
      .app_name = "lol",
      .transparent_window = true,
      .hot_reload_enabled = true,
  });

  on_hide_mouse_change();
  renderer_.emplace();
  ResourceManager::init(ResourceManager::CreateInfo{.renderer = &renderer_.value()});
  renderer_->init(gfx::MemeRenderer123::CreateInfo{
      .device = device_.get(),
      .window = window_.get(),
      .resource_dir = resource_dir_,
      .config_file_path = local_resource_dir_ / "renderer_config.txt",
  });
  // voxel_renderer_ = std::make_unique<vox::Renderer>();
  // voxel_renderer_->init(&renderer_);
  // voxel_world_ = std::make_unique<vox::World>();
  // voxel_world_->init(voxel_renderer_.get(), &renderer_, resource_dir_);
  camera_.pos.x = -5;
  init_camera();
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
  int scene = 0;
  // auto load_grid = [&](int radius, float dist, const std::string& path, float scale = 1.0f) {
  //   glm::ivec3 iter{};
  //   glm::ivec3 dims{radius, 1, radius};
  //   for (iter.z = -dims.z; iter.z <= dims.z; iter.z++) {
  //     for (iter.x = -dims.x; iter.x <= dims.x; iter.x++) {
  //       glm::vec3 pos = glm::vec3{iter} * dist;
  //       load_model(path, glm::translate(glm::scale(glm::mat4{1}, glm::vec3(scale)), pos));
  //     }
  //   }
  // };
  const char* sponza_path = "Models/Sponza/glTF_ktx2/Sponza.gltf";
  // [[maybe_unused]] auto chessboards = [&]() {
  //   load_grid(4, 1.0, "Models/ABeautifulGame/glTF_ktx2/ABeautifulGame.gltf", 10);
  // };
  // [[maybe_unused]] auto sponzas = [&]() { load_grid(10, 40.0, sponza_path); };
  // chessboards();
  // sponzas();
  [[maybe_unused]] auto sponza_single = [&]() { load_model(sponza_path, glm::mat4{1}); };
  sponza_single();

  if (scene == -1) {
    glm::ivec3 iter{};
    int n = 4;
    glm::ivec3 dims{n, 1, n};
    float dist = 1.0;
    for (iter.z = -dims.z; iter.z <= dims.z; iter.z++) {
      for (iter.x = -dims.x; iter.x <= dims.x; iter.x++) {
        glm::vec3 pos = glm::vec3{iter} * dist;
        load_model(config_.paths[1], glm::translate(glm::mat4{1}, pos));
      }
    }
    // load_model(config_.paths[1], glm::mat4{1});
  } else if (scene == 1) {
    rando::seed(10000000);
    size_t count = 300'000;
    float scale = 10;
    float radius = 8000;

    for (size_t i = 0; i < count; i++) {
      auto rand_f = [radius]() { return rando::get_float(-radius, radius); };
      auto pos = glm::vec3{rand_f(), rand_f(), rand_f()};
      glm::vec3 randomAxis = glm::linearRand(glm::vec3(-1.0f), glm::vec3(1.0f));
      float randomAngle = glm::linearRand(0.0f, glm::two_pi<float>());
      auto rot = glm::angleAxis(randomAngle, glm::normalize(randomAxis));
      load_model(config_.paths[2], glm::translate(glm::mat4{1}, pos) * glm::mat4_cast(rot) *
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
    load_model(config_.paths[1], glm::scale(glm::mat4{1}, glm::vec3{1}));
    // load_model(config_.paths[1], glm::translate(glm::mat4{1}, glm::vec3{0, 1, 0}));
  }

  std::vector<float> frame_times;
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
    frame_times.push_back(dt);
    if (frame_times.size() > 100) {
      frame_times.erase(frame_times.begin());
    }
    avg_dt_ = std::accumulate(frame_times.begin(), frame_times.end(), 0.f) / frame_times.size();
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

    const gfx::RenderArgs args{
        .view_mat = camera_.get_view_mat(),
        .camera_pos = camera_.pos,
        .clear_color = config_.clear_color,
        .draw_imgui = imgui_enabled_,
    };
    renderer_->render(args);

    ImGui::EndFrame();
  }

  // if (voxel_world_) {
  //   voxel_world_->shutdown();
  // }

  write_config();
  write_camera();
  renderer_->shutdown();
  renderer_.reset();
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
    if (key == GLFW_KEY_C && mods & GLFW_MOD_CONTROL && mods & GLFW_MOD_SHIFT) {
      camera_ = {};
      camera_.calc_vectors();
    } else if (key == GLFW_KEY_C) {
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
  renderer_->on_key_event(key, action, mods);
}

void App::on_hide_mouse_change() {
  glfwSetInputMode(window_->get_handle(), GLFW_CURSOR,
                   hide_mouse_ ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void App::load_config() {
  ZoneScoped;
  std::ifstream f(std::filesystem::exists(config_path_) ? config_path_
                                                        : resource_dir_ / "default_config.txt");
  ASSERT(f.is_open());
  std::string token;
  while (f >> token) {
    if (token == "win_dims") {
      f >> config_.win_dims.x;
      f >> config_.win_dims.y;
    } else if (token == "win_pos") {
      f >> config_.win_pos.x;
      f >> config_.win_pos.y;
    } else if (token == "paths") {
      f >> token;
      int path_count = std::stoull(token);
      for (int i = 0; i < path_count; i++) {
        f >> token;
        config_.paths.emplace_back(token);
      }
    } else if (token == "clear_color") {
      f >> config_.clear_color.r;
      f >> config_.clear_color.g;
      f >> config_.clear_color.b;
      f >> config_.clear_color.a;
    }
  }
}

void App::write_config() {
  std::ofstream f(config_path_);
  auto win_dims = window_->get_window_not_framebuffer_size();
  f << "win_dims " << win_dims.x << ' ' << win_dims.y << '\n';
  auto win_pos = window_->get_window_position();
  f << "win_pos " << win_pos.x << ' ' << win_pos.y << '\n';
  f << "clear_color " << config_.clear_color.r << ' ' << config_.clear_color.g << ' '
    << config_.clear_color.b << ' ' << config_.clear_color.a << '\n';
  f << "paths " << config_.paths.size() << '\n';
  for (const auto& path : config_.paths) {
    f << path.generic_string() << '\n';
  }
}

void App::on_imgui(float) {
  ImGui::Begin("Renderer");
  ImGui::Text("Avg time %f (ms)", avg_dt_ * 1000.f);
  renderer_->on_imgui();
  if (ImGui::TreeNodeEx("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::DragFloat3("Position", &camera_.pos.x, 0.1f);
    ImGui::DragFloat("Acceleration Strength", &camera_.acceleration_strength, 0.1f, 0.1f, 1000.f);
    ImGui::DragFloat("Mouse Sensitivity", &camera_.mouse_sensitivity, 0.01f, 0.01f, 1.f);
    ImGui::TreePop();
  }
  ImGui::ColorEdit4("Clear Color", &config_.clear_color.r, ImGuiColorEditFlags_Float);
  ImGui::End();
  ImGui::ShowDemoWindow();
}

void App::load_model(const std::string& path, const glm::mat4& transform) {
  std::string full_path;
  if (path.starts_with("Models")) {
    full_path = resource_dir_ / "models" / "gltf" / path;
  } else {
    full_path = path;
  }
  models_.emplace_back(ResourceManager::get().load_model(full_path, transform));
}

void App::init_camera() {
  std::ifstream f(camera_path_);
  if (!f.is_open()) {
    return;
  }
  f >> camera_.pos.x >> camera_.pos.y >> camera_.pos.z;
  f >> camera_.pitch >> camera_.yaw;
  f >> camera_.acceleration_strength >> camera_.max_velocity;
  camera_.calc_vectors();
}

void App::write_camera() {
  std::ofstream f(camera_path_);
  f << camera_.pos.x << ' ' << camera_.pos.y << ' ' << camera_.pos.z << '\n';
  f << camera_.pitch << ' ' << camera_.yaw << '\n';
  f << camera_.acceleration_strength << ' ' << camera_.max_velocity << '\n';
}
