#include "TestRenderer.hpp"

#include "Window.hpp"
#include "core/Logger.hpp"  // IWYU pragma: keep
#include "gfx/ShaderManager.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/Swapchain.hpp"

using namespace teng;
using namespace teng::gfx;
using namespace teng::gfx::rhi;

namespace teng::gfx {

TestRenderer::TestRenderer(const CreateInfo& cinfo)
    : active_scene_(cinfo.initial_scene),
      device_(cinfo.device),
      swapchain_(cinfo.swapchain),
      frame_gpu_upload_allocator_(device_, false),
      buffer_copy_mgr_(device_, frame_gpu_upload_allocator_),
      window_(cinfo.window) {
  shader_mgr_ = std::make_unique<gfx::ShaderManager>();
  shader_mgr_->init(
      device_, gfx::ShaderManager::Options{.targets = device_->get_supported_shader_targets()});
  rg_.init(device_);
  scene_ = create_test_scene(active_scene_);
  scene_->init(make_ctx());
}

TestSceneContext TestRenderer::make_ctx() {
  return {.device = device_,
          .swapchain = swapchain_,
          .window = window_,
          .shader_mgr = shader_mgr_.get(),
          .rg = &rg_,
          .buffer_copy = &buffer_copy_mgr_};
}

void TestRenderer::set_scene(TestDebugScene id) {
  if (scene_) {
    scene_->shutdown();
    scene_.reset();
  }
  active_scene_ = id;
  scene_ = create_test_scene(id);
  scene_->init(make_ctx());
  LINFO("vktest scene: {}", to_string(id));
}

void TestRenderer::cycle_debug_scene() {
  auto next = static_cast<uint8_t>(static_cast<uint8_t>(active_scene_) + 1u) %
              static_cast<uint8_t>(TestDebugScene::Count);
  set_scene(static_cast<TestDebugScene>(next));
}

void TestRenderer::render() {
  shader_mgr_->replace_dirty_pipelines();
  add_render_graph_passes();
  static int i = 0;
  bool verbose = i++ == 0;
  device_->acquire_next_swapchain_image(swapchain_);
  rg_.bake(window_->get_window_size(), verbose);
  rg_.execute();
  device_->submit_frame();
}

TestRenderer::~TestRenderer() {
  if (scene_) {
    scene_->shutdown();
    scene_.reset();
  }
  shader_mgr_->shutdown();
}

void TestRenderer::recreate_resources_on_swapchain_resize() {
  if (scene_) {
    scene_->on_swapchain_resize(make_ctx());
  }
}

void TestRenderer::add_render_graph_passes() { scene_->add_render_graph_passes(make_ctx()); }

}  // namespace teng::gfx
