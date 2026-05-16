#include "engine/Engine.hpp"

#include <GLFW/glfw3.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <ranges>
#include <tracy/Tracy.hpp>
#include <utility>
#include <vector>

#include "Window.hpp"
#include "core/CVar.hpp"
#include "core/Diagnostic.hpp"
#include "core/EAssert.hpp"
#include "core/Logger.hpp"
#include "core/TomlUtil.hpp"
#include "engine/assets/AssetService.hpp"
#include "engine/render/RenderService.hpp"
#include "engine/scene/CoreComponentRegistrar.hpp"
#include "engine/scene/SceneComponentContext.hpp"
#include "engine/scene/SceneCooked.hpp"
#include "engine/scene/SceneSerialization.hpp"
#include "engine/scene/SceneSerializationContext.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/Swapchain.hpp"
#include "imgui.h"

namespace teng::engine {

namespace {

std::filesystem::path find_resource_dir() {
  std::filesystem::path curr_path = std::filesystem::current_path();
  while (curr_path.has_parent_path()) {
    if (std::filesystem::exists(curr_path / "resources")) {
      return curr_path / "resources";
    }
    curr_path = curr_path.parent_path();
  }
  return {};
}

}  // namespace

LayerStack::~LayerStack() { clear(); }

void LayerStack::set_context(EngineContext* ctx) { ctx_ = ctx; }

void LayerStack::push_layer(std::unique_ptr<Layer> layer) {
  ASSERT(layer);
  layers_.push_back(std::move(layer));
  if (ctx_) {
    layers_.back()->on_attach(*ctx_);
  }
}

void LayerStack::clear() {
  if (ctx_) {
    for (auto& layer : std::views::reverse(layers_)) {
      layer->on_detach(*ctx_);
    }
  }
  layers_.clear();
}

void LayerStack::dispatch_key_event(int key, int action, int mods) {
  ASSERT(ctx_);
  for (auto& layer : layers_) {
    layer->on_key_event(*ctx_, key, action, mods);
  }
}

void LayerStack::dispatch_cursor_pos(double x, double y) {
  ASSERT(ctx_);
  for (auto& layer : layers_) {
    layer->on_cursor_pos(*ctx_, x, y);
  }
}

void LayerStack::update(const EngineTime& time) {
  ASSERT(ctx_);
  for (auto& layer : layers_) {
    layer->on_update(*ctx_, time);
  }
}

void LayerStack::imgui() {
  ASSERT(ctx_);
  for (auto& layer : layers_) {
    layer->on_imgui(*ctx_);
  }
}

void LayerStack::render_scene() {
  ASSERT(ctx_);
  for (auto& layer : layers_) {
    layer->on_render_scene(*ctx_);
  }
}

void LayerStack::render() {
  ASSERT(ctx_);
  for (auto& layer : layers_) {
    layer->on_render(*ctx_);
  }
}

void LayerStack::end_frame() {
  ASSERT(ctx_);
  for (auto& layer : layers_) {
    layer->on_end_frame(*ctx_);
  }
}

Engine::Engine(EngineConfig config)
    : config_(std::move(config)), imgui_enabled_(config_.enable_imgui) {
  init();
}

Engine::~Engine() = default;

void Engine::init() {
  ZoneScoped;
  if (initialized_) {
    return;
  }

  init_resource_paths();
  std::filesystem::current_path(resource_dir_.parent_path());
  local_resource_dir_ = resource_dir_ / "local";
  if (!std::filesystem::exists(local_resource_dir_)) {
    std::filesystem::create_directories(local_resource_dir_);
  }
  CVarSystem::get().load_from_file((local_resource_dir_ / "cvars.txt").string());

  std::vector<scene::ComponentModuleDescriptor> component_modules;
  const std::span<const scene::ComponentModuleDescriptor> core_modules = core_component_modules();
  component_modules.insert(component_modules.end(), core_modules.begin(), core_modules.end());
  core::DiagnosticReport report;
  component_registry_ = std::make_unique<scene::ComponentRegistry>();
  if (!scene::try_freeze_component_registry(component_modules, *component_registry_, report)) {
    LCRITICAL("Failed to freeze component registry: {}", report.to_string());
    std::exit(1);
  }

  frozen_scene_component_ctx_ = std::make_unique<FlecsComponentContext>();
  if (!make_flecs_component_context(*component_registry_, *frozen_scene_component_ctx_, report)) {
    LCRITICAL("Failed to freeze scene component context: {}", report.to_string());
    std::exit(1);
  }

  scene_serialization_ctx_ = std::make_unique<SceneSerializationContext>(
      make_scene_serialization_context(*component_registry_));
  if (!scene_serialization_ctx_) {
    LCRITICAL("Failed to freeze scene serialization context: {}", report.to_string());
    std::exit(1);
  }

  scenes_ = std::make_unique<SceneManager>(*frozen_scene_component_ctx_);

  window_ = create_platform_window();
  Window::InitInfo win_init_info{
      .key_callback_fn =
          [this](int key, int action, int mods) {
            pending_key_events_.push_back(KeyEvent{.key = key, .action = action, .mods = mods});
          },
      .cursor_pos_callback_fn =
          [this](double x, double y) {
            pending_cursor_events_.push_back(CursorEvent{.x = x, .y = y});
          },
      .win_dims_x = config_.initial_window_width,
      .win_dims_y = config_.initial_window_height,
      .floating_window = config_.floating_window,
  };
  window_->init(win_init_info);
  window_->set_window_position(config_.initial_window_position);

  device_ = gfx::rhi::create_device(resolve_gfx_api());
  device_->init({
      .shader_lib_dir = resource_dir_ / "shader_out",
      .app_name = config_.app_name,
      .frames_in_flight = 3,
  });

  const auto win_dims = window_->get_window_size();
  swapchain_ = device_->create_swapchain_h(gfx::rhi::SwapchainDesc{
      .window = window_.get(),
      .width = win_dims.x,
      .height = win_dims.y,
      .vsync = config_.vsync,
  });

  time_ = {};
  context_.window_ = window_.get();
  context_.device_ = device_.get();
  context_.swapchain_ = device_->get_swapchain(swapchain_);
  context_.resource_dir_ = &resource_dir_;
  context_.local_resource_dir_ = &local_resource_dir_;
  assets_ = std::make_unique<assets::AssetService>(assets::AssetServiceConfig{
      .project_root = resource_dir_.parent_path(),
      .content_root = resource_dir_.filename(),
  });
  (void)assets_->scan();
  context_.assets_ = assets_.get();
  context_.scenes_ = scenes_.get();
  context_.component_registry_ = component_registry_.get();
  context_.scene_serialization_ = scene_serialization_ctx_.get();
  context_.time_ = &time_;
  context_.input_ = &input_snapshot_;
  context_.imgui_enabled_ = &imgui_enabled_;

  renderer_ = std::make_unique<RenderService>(RenderService::CreateInfo{
      .device = device_.get(),
      .swapchain = context_.swapchain_,
      .window = window_.get(),
      .scenes = scenes_.get(),
      .assets = assets_.get(),
      .time = &time_,
      .resource_dir = resource_dir_,
      .imgui_ui_active = imgui_enabled_,
  });
  context_.renderer_ = renderer_.get();
  layers_.set_context(&context_);

  initialized_ = true;
}

void Engine::init_resource_paths() {
  resource_dir_ = config_.resource_dir.empty() ? find_resource_dir() : config_.resource_dir;
  if (resource_dir_.empty()) {
    LCRITICAL("Unable to locate resources directory");
    std::exit(1);
  }
}

gfx::rhi::GfxAPI Engine::resolve_gfx_api() const {
  switch (config_.preferred_gfx_api) {
    case EngineGfxApi::Vulkan:
#ifndef VULKAN_BACKEND
      LCRITICAL("Vulkan backend requested but not compiled");
      std::exit(1);
#else
      return gfx::rhi::GfxAPI::Vulkan;
#endif
    case EngineGfxApi::Metal:
#ifndef METAL_BACKEND
      LCRITICAL("Metal backend requested but not compiled");
      std::exit(1);
#else
      return gfx::rhi::GfxAPI::Metal;
#endif
    case EngineGfxApi::PlatformDefault:
    default:
#if defined(__APPLE__) && defined(METAL_BACKEND)
      LINFO("Defaulting to Metal backend");
      return gfx::rhi::GfxAPI::Metal;
#elifdef VULKAN_BACKEND
      LINFO("Defaulting to Vulkan backend");
      return gfx::rhi::GfxAPI::Vulkan;
#else
      LCRITICAL("No graphics backend compiled");
      std::exit(1);
#endif
  }
}

void Engine::dispatch_pending_events() {
  for (const auto& event : pending_cursor_events_) {
    const glm::vec2 pos{static_cast<float>(event.x), static_cast<float>(event.y)};
    if (have_cursor_pos_) {
      input_snapshot_.cursor_delta +=
          glm::vec2{pos.x - last_cursor_pos_.x, last_cursor_pos_.y - pos.y};
    }
    last_cursor_pos_ = pos;
    have_cursor_pos_ = true;
    layers_.dispatch_cursor_pos(event.x, event.y);
  }
  pending_cursor_events_.clear();

  for (const auto& event : pending_key_events_) {
    if (event.action == GLFW_PRESS) {
      input_snapshot_.held_keys.insert(event.key);
      input_snapshot_.pressed_keys.insert(event.key);
    } else if (event.action == GLFW_RELEASE) {
      input_snapshot_.held_keys.erase(event.key);
      input_snapshot_.released_keys.insert(event.key);
    }
    layers_.dispatch_key_event(event.key, event.action, event.mods);
  }
  pending_key_events_.clear();
}

void Engine::refresh_input_snapshot_ui_state() {
  input_snapshot_.imgui_blocks_keyboard = false;
  if (imgui_enabled_ && ImGui::GetCurrentContext()) {
    const ImGuiIO& io = ImGui::GetIO();
    input_snapshot_.imgui_blocks_keyboard = io.WantTextInput || io.WantCaptureKeyboard;
  }
}

void Engine::clear_transient_input() {
  input_snapshot_.pressed_keys.clear();
  input_snapshot_.released_keys.clear();
  input_snapshot_.cursor_delta = {};
}

Result<SceneLoadResult> Engine::load_scene(const std::filesystem::path& scene_path) {
  if (is_cooked_scene_file_path(scene_path)) {
    return load_cooked_scene_file(*scenes_, *scene_serialization_ctx_, scene_path);
  }
  return load_scene_file(*scenes_, *scene_serialization_ctx_, scene_path);
}

Result<SceneLoadResult> Engine::load_project_startup_scene() {
  const std::filesystem::path project_path = resource_dir_ / "project.toml";
  Result<toml::table> project = parse_toml_file(project_path);
  if (!project) {
    return make_unexpected("failed to load project config " + project_path.string() + ": " +
                           project.error());
  }

  const std::optional<int64_t> schema_version = (*project)["schema_version"].value<int64_t>();
  if (!schema_version || *schema_version != 1) {
    return make_unexpected("project config schema_version must be 1");
  }

  const std::optional<std::string> startup_scene = (*project)["startup_scene"].value<std::string>();
  if (!startup_scene || startup_scene->empty()) {
    return make_unexpected("project config is missing startup_scene");
  }
  return load_scene(*startup_scene);
}

bool Engine::tick() {
  ZoneScoped;
  if (shutting_down_ || !initialized_ || window_->should_close()) {
    return false;
  }

  window_->poll_events();
  dispatch_pending_events();
  refresh_input_snapshot_ui_state();

  const double curr_time = glfwGetTime();
  time_.total_seconds = curr_time;
  time_.delta_seconds = have_prev_time_ ? static_cast<float>(curr_time - prev_time_seconds_) : 0.f;
  time_.frame_index = completed_frames_;
  prev_time_seconds_ = curr_time;
  have_prev_time_ = true;
  input_snapshot_.delta_seconds = time_.delta_seconds;

  const SceneExecutionPolicy active_scene_policy = scenes_->active_scene_execution_policy();
  if (active_scene_policy.receives_active_input && scenes_->active_scene()) {
    Scene* active_scene = scenes_->active_scene();
    active_scene->set_input_snapshot(input_snapshot_);
  }
  if (active_scene_policy.advances_simulation && !scenes_->tick_active_scene(time_.delta_seconds)) {
    return false;
  }
  clear_transient_input();

  renderer_->begin_frame();

  layers_.update(time_);
  layers_.imgui();
  layers_.render_scene();
  if (scenes_->active_scene() && !renderer_->scene_submitted_this_frame()) {
    renderer_->enqueue_active_scene();
  }
  layers_.render();
  renderer_->end_frame();
  layers_.end_frame();

  ++completed_frames_;
  if (config_.quit_after_frames.has_value() && completed_frames_ >= *config_.quit_after_frames) {
    return false;
  }
  return !window_->should_close();
}

void Engine::run() {
  while (tick()) {
  }
  shutdown();
}

void Engine::shutdown() {
  ZoneScoped;
  if (shutting_down_ || !initialized_) {
    return;
  }
  shutting_down_ = true;
  CVarSystem::get().save_to_file((local_resource_dir_ / "cvars.txt").string());
  layers_.clear();
  renderer_->shutdown();
  renderer_.reset();
  assets_.reset();
  swapchain_ = {};
  window_->shutdown();
  device_->shutdown();
  context_ = {};
  window_.reset();
  device_.reset();
  initialized_ = false;
}

}  // namespace teng::engine
