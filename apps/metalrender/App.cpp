#include "App.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <glm/ext/matrix_transform.hpp>

#include "ResourceManager.hpp"
#include "UI.hpp"
#include "Util.hpp"
#include "Window.hpp"
#include "core/CVar.hpp"
#include "core/Console.hpp"
#include "core/Logger.hpp"
#include "gfx/MemeRenderer123.hpp"
#include "gfx/renderer/RendererCVars.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/Swapchain.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "implot.h"
#include "tracy/Tracy.hpp"

using namespace teng;
using namespace teng::gfx;
using namespace teng::demo_scenes;

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
  register_cvar_console(console_);
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
#ifdef __APPLE__
  device_ = gfx::rhi::create_device(gfx::rhi::GfxAPI::Metal);
#else
  device_ = gfx::rhi::create_device(gfx::rhi::GfxAPI::Vulkan);
#endif
  device_->init({
      .shader_lib_dir = resource_dir_ / "shader_out",
      .app_name = "lol",
      .frames_in_flight = 3,
  });
  auto win_dims = window_->get_window_size();
  swapchain_ = device_->create_swapchain_h(gfx::rhi::SwapchainDesc{
      .window = window_.get(),
      .width = win_dims.x,
      .height = win_dims.y,
      .vsync = true,
  });
  fps_camera_.set_mouse_captured(window_->get_handle(), false);

  // TODO: lol
  constexpr bool mesh_shaders_capable = true;
  gfx::apply_renderer_cvar_device_constraints(mesh_shaders_capable);

  renderer_ = std::make_unique<gfx::MemeRenderer123>(gfx::MemeRenderer123::CreateInfo{
      .device = device_.get(),
      .swapchain = device_->get_swapchain(swapchain_),
      .window = window_.get(),
      .resource_dir = resource_dir_,
  });
  ResourceManager::init(
      ResourceManager::CreateInfo{.model_gpu_mgr = renderer_->get_model_gpu_mgr()});
  fps_camera_.camera().pos.x = -5;
  init_camera();

  load_scene_presets();
  demo_scenes::seed_demo_scene_rng(10000000);
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
    const bool imgui_blocks_keyboard =
        imgui_enabled_ && (ImGui::GetIO().WantTextInput || ImGui::GetIO().WantCaptureKeyboard);
    fps_camera_.update(window_->get_handle(), dt, imgui_blocks_keyboard);

    // if (voxel_world_) {
    //   voxel_world_->update(dt, camera_);
    // }

    if (imgui_enabled_) {
      on_imgui(dt);
      ImGui::Render();
    }

    ResourceManager::get().update();

    for (const auto& model_handle : models_) {
      auto* model = ResourceManager::get().get_model(model_handle);
      bool dirty = model->update_transforms();
      if (dirty) {
        renderer_->update_model_instance_transforms(*model);
      }
    }

    renderer_->render({
        .view_mat = fps_camera_.camera().get_view_mat(),
        .camera_pos = fps_camera_.camera().pos,
        .clear_color = config_.clear_color,
    });
    if (imgui_enabled_) {
      ImGui::EndFrame();
    }
  }
  shutdown();
}

void App::on_curse_pos_event(double xpos, double ypos) { fps_camera_.on_cursor_pos(xpos, ypos); }

void App::on_key_event(int key, int action, [[maybe_unused]] int mods) {
  const auto is_press = action == GLFW_PRESS;
  if (is_press && key == GLFW_KEY_SLASH) {
    if (!imgui_enabled_) {
      imgui_enabled_ = true;
      renderer_->set_imgui_enabled(true);
      console_forced_imgui_ = true;
    }
    console_.open();
    return;
  }

  if (console_.is_open() && ImGui::GetIO().WantTextInput) {
    return;
  }

  if (is_press) {
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9 && mods & GLFW_MOD_SUPER) {
      int idx = key - GLFW_KEY_0;
      if (idx < (int)scene_presets_.size()) {
        clear_all_models();
        fps_camera_.camera() = scene_presets_[idx].cam;
        fps_camera_.camera().calc_vectors();
        scene_presets_[idx].load_fn();
      }
    }

    if (key == GLFW_KEY_ESCAPE) {
      fps_camera_.toggle_mouse_capture(window_->get_handle());
    }
    if (key == GLFW_KEY_G && mods & GLFW_MOD_ALT) {
      imgui_enabled_ = !imgui_enabled_;
      renderer_->set_imgui_enabled(imgui_enabled_);
    }
    if (key == GLFW_KEY_C && mods & GLFW_MOD_CONTROL && mods & GLFW_MOD_SHIFT) {
      fps_camera_.camera() = {};
      fps_camera_.camera().calc_vectors();
    } else if (key == GLFW_KEY_C) {
      if (!models_.empty()) {
        ResourceManager::get().free_model(models_[0]);
        models_.erase(models_.begin());
      }
    } else if (key == GLFW_KEY_L && mods & GLFW_MOD_CONTROL) {
      static float h = 40.f;
      load_model(demo_scenes::k_sponza_path, glm::translate(glm::mat4{1}, glm::vec3{0, h, 0}));
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

void App::load_config() {
  CVarSystem::get().load_from_file(local_resource_dir_ / "cvars.txt");
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
  ZoneScoped;
  const std::filesystem::path cvar_path = local_resource_dir_ / "cvars.txt";
  CVarSystem::get().save_to_file(cvar_path);

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
  push_font("ComicMono", 16);
  const bool was_open = console_.is_open();
  console_.draw_imgui();
  const bool is_open = console_.is_open();
  if (was_open && !is_open && console_forced_imgui_) {
    imgui_enabled_ = false;
    renderer_->set_imgui_enabled(false);
    console_forced_imgui_ = false;
  }
  ImPlot::ShowDemoWindow();

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
    return std::sqrt(variance_sum / data.size());
  };

  float total = 0.f;
  int n = std::min(10, frame_times.data.size());
  for (int i = 0; i < n; i++) {
    total +=
        frame_times
            .data[(frame_times.offset + frame_times.data.size() - 1 - i) % frame_times.data.size()]
            .y;
  }
  auto frame_time_ms = total / n;

  ImGui::Begin("metalrender");

  ImGui::Text("CPU: %5.2f ms/frame (%5.2f FPS)", frame_time_ms, 1000.f / frame_time_ms);
  ImGui::Text("GPU: %5.2f ms/frame (%5.2f FPS)", renderer_->get_stats().avg_gpu_frame_time,
              1000.f / renderer_->get_stats().avg_gpu_frame_time);

  if (ImGui::BeginTabBar("Renderer##MainTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
    if (ImGui::BeginTabItem("Renderer")) {
      renderer_->on_imgui();
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Frame Times")) {
      ImGui::Text("Avg CPU time %f (ms), %.2f FPS", frame_time_ms, 1000.f / frame_time_ms);
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
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Window")) {
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
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Camera")) {
      ImGui::DragFloat3("Position", &fps_camera_.camera().pos.x, 0.1f);
      ImGui::DragFloat("Move Speed", &fps_camera_.camera().move_speed, 0.1f, 0.1f, 1000.f);
      ImGui::Text("Pitch: %.1f Yaw: %.1f", fps_camera_.camera().pitch, fps_camera_.camera().yaw);
      ImGui::DragFloat("Mouse Sensitivity", &fps_camera_.camera().mouse_sensitivity, 0.01f, 0.01f,
                       1.f);
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Scene")) {
      ImGui::Text("Loaded Models: %zu", ResourceManager::get().get_tot_models_loaded());
      ImGui::Text("Total Model Instances: %zu", ResourceManager::get().get_tot_instances_loaded());
      if (ImGui::TreeNode("Models")) {
        for (const auto& model_handle : models_) {
          auto* model = ResourceManager::get().get_model(model_handle);
          ASSERT(model);
          ImGui::PushID(model_handle.to64());
          imgui_node(0, *model);
          ImGui::PopID();
        }
        ImGui::TreePop();
      }

      ImGui::SeparatorText("Presets");
      static int scene_preset_selection = 0;
      if (!scene_presets_.empty()) {
        scene_preset_selection =
            std::clamp(scene_preset_selection, 0, static_cast<int>(scene_presets_.size()) - 1);
        const float list_h = ImGui::GetTextLineHeightWithSpacing() * 7.0f;
        if (ImGui::BeginListBox("##scene_presets", ImVec2(-FLT_MIN, list_h))) {
          for (int i = 0; i < static_cast<int>(scene_presets_.size()); ++i) {
            ImGui::PushID(i);
            const bool selected = (scene_preset_selection == i);
            if (ImGui::Selectable(scene_presets_[i].name.c_str(), selected,
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
              scene_preset_selection = i;
              if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                fps_camera_.camera() = scene_presets_[i].cam;
                fps_camera_.camera().calc_vectors();
                clear_all_models();
                scene_presets_[i].load_fn();
              }
            }
            ImGui::PopID();
          }
          ImGui::EndListBox();
        }
        if (ImGui::Button("Load preset", ImVec2(-FLT_MIN, 0))) {
          fps_camera_.camera() = scene_presets_[scene_preset_selection].cam;
          fps_camera_.camera().calc_vectors();
          clear_all_models();
          scene_presets_[scene_preset_selection].load_fn();
        }
      }
      ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
  }

  ImGui::ColorEdit4("Clear Color", &config_.clear_color.r, ImGuiColorEditFlags_Float);
  ImGui::End();

  ImGui::ShowDemoWindow();
  ImGui::PopFont();
}

void App::load_model(const std::string& path, const glm::mat4& transform) {
  models_.emplace_back(ResourceManager::get().load_model(
      demo_scenes::resolve_model_path(resource_dir_, path), transform));
}

void App::init_camera() {
  std::ifstream f(camera_path_);
  if (!f.is_open()) {
    return;
  }
  int version;
  f >> version;
  if (version != k_camera_config_version) {
    fps_camera_.camera() = {};
  } else {
    std::string token;
    while (f >> token) {
      if (token == "pos") {
        f >> fps_camera_.camera().pos.x >> fps_camera_.camera().pos.y >> fps_camera_.camera().pos.z;
      } else if (token == "pitch_yaw") {
        f >> fps_camera_.camera().pitch >> fps_camera_.camera().yaw;
      } else if (token == "move_speed") {
        f >> fps_camera_.camera().move_speed;
      }
    }
  }
  fps_camera_.camera().calc_vectors();
}

void App::write_camera() {
  std::ofstream f(camera_path_);
  f << k_camera_config_version << '\n';
  f << "pos " << fps_camera_.camera().pos.x << ' ' << fps_camera_.camera().pos.y << ' '
    << fps_camera_.camera().pos.z << '\n';
  f << "pitch_yaw " << fps_camera_.camera().pitch << ' ' << fps_camera_.camera().yaw << '\n';
  f << "move_speed " << fps_camera_.camera().move_speed << '\n';
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

void App::clear_all_models() {
  for (auto& m : models_) {
    ResourceManager::get().free_model(m);
  }
  models_.clear();
}

void App::load_scene_presets() {
  scene_presets_.clear();
  demo_scenes::ScenePresetLoaders loaders{
      .add_model =
          [this](const std::filesystem::path& p, const glm::mat4& t) {
            models_.emplace_back(ResourceManager::get().load_model(p, t));
          },
      .add_instanced =
          [this](const std::filesystem::path& p, std::vector<glm::mat4>&& tf) {
            ResourceManager::InstancedModelLoadRequest req{.path = p,
                                                           .instance_transforms = std::move(tf)};
            auto result = ResourceManager::get().load_instanced_models(std::span(&req, 1));
            for (auto& r : result) {
              models_.reserve(models_.size() + r.size());
              models_.insert(models_.end(), std::make_move_iterator(r.begin()),
                             std::make_move_iterator(r.end()));
            }
          },
  };
  demo_scenes::append_default_scene_presets(scene_presets_, resource_dir_, loaders);
}
void App::run_preset_scene(int idx) {
  auto& preset = scene_presets_[idx];
  auto old_models = std::move(models_);
  models_ = {};
  fps_camera_.camera() = preset.cam;
  fps_camera_.camera().calc_vectors();

  preset.load_fn();

  for (auto& m : old_models) {
    LINFO("freeing");
    ResourceManager::get().free_model(m);
  }
}

void App::imgui_node(int node, ModelInstance& model) {
  ImGui::PushID(node);
  if (ImGui::TreeNode("child")) {
    ImGui::Text("Model ");
    auto& local_transform = model.local_transforms[node];
    bool dirty = false;
    if (ImGui::DragFloat3("Position", &local_transform.translation.x, 0.1f)) {
      dirty = true;
    }
    if (ImGui::DragFloat("Scale", &local_transform.scale, 0.1f)) {
      dirty = true;
    }

    if (dirty) {
      model.mark_changed(node);
    }

    auto& n = model.nodes[node];
    for (int child = n.first_child; child != -1; child = model.nodes[child].next_sibling) {
      imgui_node(child, model);
    }
    ImGui::TreePop();
  }
  ImGui::PopID();
}
