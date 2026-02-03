#include "TestRenderer.hpp"

#include "core/Logger.hpp"  // IWYU pragma: keep
#include "core/Util.hpp"
#include "gfx/ShaderManager.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/Swapchain.hpp"

using namespace teng;

TestRenderer::TestRenderer(const CreateInfo& cinfo)
    : device_(cinfo.device), swapchain_(cinfo.swapchain) {
  shader_mgr_ = std::make_unique<gfx::ShaderManager>();
  shader_mgr_->init(
      device_, gfx::ShaderManager::Options{.targets = device_->get_supported_shader_targets()});
  clear_color_cmp_pso_ = device_->create_compute_pipeline_h({"vulkan_exp/clear_tex_to_color"});
}

void TestRenderer::render() {
  shader_mgr_->replace_dirty_pipelines();

  auto* enc = device_->begin_cmd_encoder();
  glm::vec4 clear_color{1, 1, 0, 1};
  device_->begin_swapchain_rendering(swapchain_, enc, &clear_color);
  enc->end_rendering();

  auto b = rhi::GPUBarrier::tex_barrier(swapchain_->get_current_texture(),
                                        rhi::ResourceState::ColorWrite,
                                        rhi::ResourceState::ComputeWrite);
  enc->barrier(&b);
  enc->bind_pipeline(clear_color_cmp_pso_);
  struct {
    glm::uvec2 dims;
  } pc;
  pc.dims = glm::uvec2{swapchain_->desc_.width, swapchain_->desc_.height};
  enc->push_constants(&pc, sizeof(pc));
  enc->bind_uav(swapchain_->get_current_texture(), 0);
  enc->dispatch_compute(glm::uvec3{align_divide_up(swapchain_->desc_.width, 8),
                                   align_divide_up(swapchain_->desc_.height, 8), 1},
                        glm::uvec3{8, 8, 1});

  {
    auto b = rhi::GPUBarrier::tex_barrier(swapchain_->get_current_texture(),
                                          rhi::ResourceState::ComputeWrite,
                                          rhi::ResourceState::SwapchainPresent);
    enc->barrier(&b);
  }

  enc->end_encoding();

  device_->submit_frame();
}

TestRenderer::~TestRenderer() { shader_mgr_->shutdown(); }
