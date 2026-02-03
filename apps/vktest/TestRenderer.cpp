#include "TestRenderer.hpp"

#include "gfx/ShaderManager.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/Device.hpp"

using namespace teng;

TestRenderer::TestRenderer(const CreateInfo& cinfo)
    : device_(cinfo.device), swapchain_(cinfo.swapchain) {
  shader_mgr_ = std::make_unique<gfx::ShaderManager>();
  shader_mgr_->init(
      device_, gfx::ShaderManager::Options{.targets = device_->get_supported_shader_targets()});
  // clear_color_cmp_pso_ = device_->create_compute_pipeline_h({"vulkan_exp/clear_tex_to_color"});
}

void TestRenderer::render() {
  shader_mgr_->replace_dirty_pipelines();

  auto* enc = device_->begin_cmd_encoder();
  glm::vec4 clear_color{1, 1, 0, 1};
  device_->begin_swapchain_rendering(swapchain_, enc, &clear_color);
  enc->end_rendering();

  enc->end_encoding();

  device_->submit_frame();
}

TestRenderer::~TestRenderer() { shader_mgr_->shutdown(); }
