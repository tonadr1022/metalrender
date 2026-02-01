#include "App.hpp"

#include <fstream>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/random.hpp>
#include <random>

#include "ResourceManager.hpp"
#include "Util.hpp"
#include "Window.hpp"
#include "core/Logger.hpp"
#include "gfx/MemeRenderer123.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/Swapchain.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "implot.h"
#include "tracy/Tracy.hpp"

using namespace teng;

namespace {

namespace random {

std::random_device rd{};
std::default_random_engine eng{rd()};

void seed(int seed) { eng.seed(seed); }

float get_float(float min, float max) {
  std::uniform_real_distribution<float> dist{min, max};
  return dist(eng);
}

}  // namespace random

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
  window_ = create_platform_window();
  Window::InitInfo win_init_info{
      .key_callback_fn = [this](int key, int action, int mods) { on_key_event(key, action, mods); },
      .cursor_pos_callback_fn = [this](double x_pos,
                                       double y_pos) { on_curse_pos_event(x_pos, y_pos); },
      .win_dims_x = config_.win_dims.x,
      .win_dims_y = config_.win_dims.y,
      .floating_window = false,
  };
  window_->init(win_init_info);
  window_->set_window_position(config_.win_pos);
  device_ = rhi::create_device(rhi::GfxAPI::Metal);
  device_->init({
      .shader_lib_dir = resource_dir_ / "shader_out",
      .app_name = "lol",
      .frames_in_flight = 3,
  });
  auto win_dims = window_->get_window_size();
  swapchain_ = device_->create_swapchain_h(rhi::SwapchainDesc{
      .window = window_.get(),
      .width = win_dims.x,
      .height = win_dims.y,
      .vsync = true,
  });
  on_hide_mouse_change();
  renderer_ = std::make_unique<gfx::MemeRenderer123>(gfx::MemeRenderer123::CreateInfo{
      .device = device_.get(),
      .swapchain = device_->get_swapchain(swapchain_),
      .window = window_.get(),
      .resource_dir = resource_dir_,
      .config_file_path = local_resource_dir_ / "renderer_config.txt",
  });
  ResourceManager::init(ResourceManager::CreateInfo{.renderer = renderer_.get()});
  // voxel_renderer_ = std::make_unique<vox::Renderer>();
  // voxel_renderer_->init(&renderer_);
  // voxel_world_ = std::make_unique<vox::World>();
  // voxel_world_->init(voxel_renderer_.get(), &renderer_, resource_dir_);
  camera_.pos.x = -5;
  init_camera();

  load_scene_presets();
  random::seed(10000000);
  run_preset_scene(0);
}

App::~App() = default;

void App::run() {
  ZoneScoped;
  int scene = 0;
  if (scene == -1) {
  }

  auto prev_win_size = window_->get_window_size();
  double last_time = glfwGetTime();
  while (!window_->should_close()) {
    ZoneScopedN("main loop");
    window_->poll_events();

    if (imgui_enabled_) {
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();
    }

    auto new_win_size = window_->get_window_size();
    if (prev_win_size != new_win_size) {
      auto* swapchain = device_->get_swapchain(swapchain_);
      auto desc = swapchain->desc_;
      ASSERT(desc.window == window_.get());
      desc.width = new_win_size.x;
      desc.height = new_win_size.y;
      device_->recreate_swapchain(desc, swapchain);
      prev_win_size = new_win_size;
    }

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
      ImGui::Render();
    }

    renderer_->render({
        .view_mat = camera_.get_view_mat(),
        .camera_pos = camera_.pos,
        .clear_color = config_.clear_color,
    });
    if (imgui_enabled_) {
      ImGui::EndFrame();
    }
  }
  shutdown();
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
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9 && mods & GLFW_MOD_SUPER) {
      int idx = key - GLFW_KEY_0;
      if (idx < (int)scene_presets_.size()) {
        clear_all_models();
        camera_ = scene_presets_[idx].cam;
        scene_presets_[idx].load_fn();
      }
    }

    if (key == GLFW_KEY_ESCAPE) {
      hide_mouse_ = !hide_mouse_;
      on_hide_mouse_change();
    }
    if (key == GLFW_KEY_G && mods & GLFW_MOD_ALT) {
      imgui_enabled_ = !imgui_enabled_;
      renderer_->set_imgui_enabled(imgui_enabled_);
    }
    if (key == GLFW_KEY_C && mods & GLFW_MOD_CONTROL && mods & GLFW_MOD_SHIFT) {
      camera_ = {};
      camera_.calc_vectors();
    } else if (key == GLFW_KEY_C) {
      if (!models_.empty()) {
        ResourceManager::get().free_model(models_[0]);
        models_.erase(models_.begin());
      }
    } else if (key == GLFW_KEY_L && mods & GLFW_MOD_CONTROL) {
      static float h = 40.f;
      load_model(config_.paths[1], glm::translate(glm::mat4{1}, glm::vec3{0, h, 0}));
      h += 40.f;
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
    } else if (token == "vsync") {
      f >> config_.vsync;
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
  f << "vsync " << device_->get_swapchain(swapchain_)->desc_.vsync << '\n';
}

namespace {

// ref implot_demo.cpp
struct ScrollingBuffer {
  int max_size;
  int offset;
  ImVector<ImVec2> data;
  explicit ScrollingBuffer(int max_size = 2000) {
    this->max_size = max_size;
    offset = 0;
    data.reserve(max_size);
  }
  void AddPoint(float x, float y) {
    if (data.size() < max_size) {
      data.push_back(ImVec2(x, y));
    } else {
      data[offset] = ImVec2(x, y);
      offset = (offset + 1) % max_size;
    }
  }
  void Erase() {
    if (data.size() > 0) {
      data.shrink(0);
      offset = 0;
    }
  }
};

}  // namespace

void App::on_imgui(float dt) {
  ImPlot::ShowDemoWindow();
  ImGui::Begin("Renderer");
  {  // frame times
    constexpr int num_times = 2000;
    static ScrollingBuffer frame_times{num_times};
    static float t = 0;
    frame_times.AddPoint(t, dt * 1000.f);
    t += dt;
    float history = 10;
    static ImPlotAxisFlags flags = 0;
    auto get_mean = [](std::span<ImVec2> data) {
      float sum = 0.f;
      for (auto v : data) sum += v.y;
      return sum / data.size();
    };

    auto std_dev = [](std::span<ImVec2> data, float mean) {
      float variance_sum = 0.f;
      for (auto v : data) {
        float diff = v.y - mean;
        float variance = diff * diff;
        variance_sum += variance;
      }
      return sqrt(variance_sum / data.size());
    };

    float total = 0.f;
    int n = std::min(10, frame_times.data.size());
    for (int i = 0; i < n; i++) {
      total += frame_times
                   .data[(frame_times.offset + frame_times.data.size() - 1 - i) %
                         frame_times.data.size()]
                   .y;
    }
    ImGui::Text("Last %u Avg time %f (ms)", n, total / n);
    if (ImGui::TreeNode("Frame Times")) {
      auto mean = get_mean(frame_times.data);
      auto stddev = std_dev(frame_times.data, mean);
      float min_val = std::numeric_limits<float>::max();
      float max_val = std::numeric_limits<float>::lowest();
      for (auto& v : frame_times.data) {
        min_val = std::min(min_val, v.y);
        max_val = std::max(max_val, v.y);
      }
      ImGui::Text("Mean: %.3f ms\nStd Dev: %.3f", mean, stddev);

      if (ImPlot::BeginPlot("Frame times", ImVec2(-1, ImGui::GetTextLineHeight() * 30))) {
        ImPlot::SetupAxes(nullptr, nullptr, flags, flags);
        ImPlot::SetupAxisLimits(ImAxis_X1, t - history, t, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 30);
        ImPlot::SetNextFillStyle(IMPLOT_AUTO_COL, 0.5f);
        ImPlot::PlotLine("Mouse Y", &frame_times.data[0].x, &frame_times.data[0].y,
                         frame_times.data.size(), 0, frame_times.offset, 2 * sizeof(float));
        ImPlot::EndPlot();
      }
      ImGui::TreePop();
    }
  }

  if (ImGui::TreeNodeEx("Window", ImGuiTreeNodeFlags_DefaultOpen)) {
    auto dims = window_->get_window_size();
    auto win_dims = window_->get_window_not_framebuffer_size();
    ImGui::Text("Framebuffer dims: %u %u", dims.x, dims.y);
    ImGui::Text("Window dims: %u %u", win_dims.x, win_dims.y);
    ImGui::Text("Fullscreen: %d", window_->get_fullscreen());

    auto* swapchain = device_->get_swapchain(swapchain_);
    bool vsync = swapchain->desc_.vsync;
    if (ImGui::Checkbox("VSync", &vsync)) {
      auto desc = swapchain->desc_;
      desc.vsync = vsync;
      device_->recreate_swapchain(desc, swapchain);
    }
    ImGui::TreePop();
  }

  renderer_->on_imgui();
  if (ImGui::TreeNodeEx("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::DragFloat3("Position", &camera_.pos.x, 0.1f);
    ImGui::DragFloat("Move Speed", &camera_.move_speed, 0.1f, 0.1f, 1000.f);
    ImGui::Text("Pitch: %.1f Yaw: %.1f", camera_.pitch, camera_.yaw);
    ImGui::DragFloat("Mouse Sensitivity", &camera_.mouse_sensitivity, 0.01f, 0.01f, 1.f);
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Scene")) {
    ImGui::Text("Loaded Models: %zu", ResourceManager::get().get_tot_models_loaded());
    ImGui::Text("Total Instances: %zu", ResourceManager::get().get_tot_instances_loaded());
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Load Scene")) {
    for (const auto& preset : scene_presets_) {
      if (ImGui::Button(preset.name.c_str())) {
        camera_ = preset.cam;
        clear_all_models();
        preset.load_fn();
      }
    }
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
  int version;
  f >> version;
  if (version != k_camera_config_version) {
    camera_ = {};
  } else {
    std::string token;
    while (f >> token) {
      if (token == "pos") {
        f >> camera_.pos.x >> camera_.pos.y >> camera_.pos.z;
      } else if (token == "pitch_yaw") {
        f >> camera_.pitch >> camera_.yaw;
      } else if (token == "move_speed") {
        f >> camera_.move_speed;
      }
    }
  }
  camera_.calc_vectors();
}

void App::write_camera() {
  std::ofstream f(camera_path_);
  f << k_camera_config_version << '\n';
  f << "pos " << camera_.pos.x << ' ' << camera_.pos.y << ' ' << camera_.pos.z << '\n';
  f << "pitch_yaw " << camera_.pitch << ' ' << camera_.yaw << '\n';
  f << "move_speed " << camera_.move_speed << '\n';
}

void App::shutdown() {
  // if (voxel_world_) {
  //   voxel_world_->shutdown();
  // }
  write_config();
  write_camera();
  renderer_.reset();
  ResourceManager::shutdown();
  window_->shutdown();
  swapchain_ = {};
  device_->shutdown();
}

void App::load_grid(glm::ivec3 radius, float dist, const std::string& path, float scale) {
  glm::ivec3 iter{};
  glm::ivec3 dims{radius};
  for (iter.z = -dims.z; iter.z <= dims.z; iter.z++) {
    for (iter.y = -dims.y; iter.y <= dims.y; iter.y++) {
      for (iter.x = -dims.x; iter.x <= dims.x; iter.x++) {
        glm::vec3 pos = glm::vec3{iter} * dist;
        load_model(path, glm::translate(glm::scale(glm::mat4{1}, glm::vec3(scale)), pos));
      }
    }
  }
};

void App::load_grid(int radius, float dist, const std::string& path, float scale) {
  glm::ivec3 iter{};
  glm::ivec3 dims{radius, 1, radius};
  for (iter.z = -dims.z; iter.z <= dims.z; iter.z++) {
    for (iter.x = -dims.x; iter.x <= dims.x; iter.x++) {
      glm::vec3 pos = glm::vec3{iter} * dist;
      load_model(path, glm::translate(glm::scale(glm::mat4{1}, glm::vec3(scale)), pos));
    }
  }
};

void App::clear_all_models() {
  for (auto& m : models_) {
    ResourceManager::get().free_model(m);
  }
  models_.clear();
}

void App::load_scene_presets() {
  constexpr const char* sponza_path = "Models/Sponza/glTF_ktx2/Sponza.gltf";
  constexpr const char* chessboard_path = "Models/ABeautifulGame/glTF_ktx2/ABeautifulGame.gltf";
  constexpr const char* suzanne_path = "Models/Suzanne/glTF/Suzanne.gltf";
  constexpr const char* cube_path = "Models/Cube/glTF/Cube.gltf";
  scene_presets_.clear();
  scene_presets_.reserve(10);
  scene_presets_.emplace_back([&, this]() { load_model(sponza_path); }, "sponza",
                              Camera{.pos = {-6, 2.5, 0}, .move_speed = 2.0f});
  scene_presets_.emplace_back(
      [&, this]() { load_grid(glm::ivec3{2, 1, 2}, 40.0, sponza_path); }, "sponza grid",
      Camera{.pos = {100, 100, -100}, .pitch = -40, .yaw = 145, .move_speed = 15.f});
  scene_presets_.emplace_back(
      [&, this]() { load_model(chessboard_path); }, "chessboard",
      Camera{.pos = {0.4, 0.4, 0.4}, .pitch = -30, .yaw = -130, .move_speed = .25f});
  scene_presets_.emplace_back(
      [&, this]() { load_grid(glm::ivec3{4, 0, 4}, 1.0, chessboard_path, 10.0); },
      "chessboard grid", Camera{.pos = {-30, 10, -20}, .pitch = -25, .yaw = 40, .move_speed = 2.f});
  scene_presets_.emplace_back([&, this]() { load_model(suzanne_path); }, "suzanne",
                              Camera{.pos = {0, 0, 3}, .pitch = 0, .yaw = 270, .move_speed = 1.f});
  scene_presets_.emplace_back([&, this]() { load_random_of_model(3000, 30.f, 1000, suzanne_path); },
                              "random suzannes",
                              Camera{.pos = {0, 0, 3}, .pitch = 0, .yaw = 270, .move_speed = 1.f});
  scene_presets_.emplace_back([&, this]() { load_random_of_model(50'000, 10.f, 3000, cube_path); },
                              "random cubes",
                              Camera{.pos = {0, 0, 3}, .pitch = 0, .yaw = 270, .move_speed = 1.f});
  scene_presets_.emplace_back([this]() { load_model("/Users/tony/models/Bistro_Godot_opt.glb"); },
                              "bistro");
  scene_presets_.emplace_back([this]() { load_grid(glm::ivec3{18, 0, 18}, 40.0, sponza_path); },
                              "many many sponzas");
}
void App::run_preset_scene(int idx) {
  auto& preset = scene_presets_[idx];
  clear_all_models();
  camera_ = preset.cam;
  preset.load_fn();
}

void App::load_random_of_model(size_t count, float scale, float radius, const std::string& path) {
  for (size_t i = 0; i < count; i++) {
    auto rand_f = [radius]() { return random::get_float(-radius, radius); };
    auto pos = glm::vec3{rand_f(), rand_f(), rand_f()};
    glm::vec3 randomAxis = glm::linearRand(glm::vec3(-1.0f), glm::vec3(1.0f));
    float randomAngle = glm::linearRand(0.0f, glm::two_pi<float>());
    auto rot = glm::angleAxis(randomAngle, glm::normalize(randomAxis));
    load_model(path, glm::translate(glm::mat4{1}, pos) * glm::mat4_cast(rot) *
                         glm::scale(glm::mat4{1}, glm::vec3{scale}));
  }
}
