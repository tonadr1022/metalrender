#include "TestRenderer.hpp"

#include "Window.hpp"
#include "core/Logger.hpp"  // IWYU pragma: keep
#include "core/Util.hpp"
#include "gfx/ShaderManager.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/Swapchain.hpp"
#include "gfx/rhi/Texture.hpp"
#include "hlsl/default_vertex.h"

using namespace teng;
using namespace teng::rhi;

namespace teng::gfx {

TestRenderer::TestRenderer(const CreateInfo& cinfo)
    : device_(cinfo.device),
      swapchain_(cinfo.swapchain),
      frame_gpu_upload_allocator_(device_),
      buffer_copy_mgr_(device_, frame_gpu_upload_allocator_),
      window_(cinfo.window) {
  shader_mgr_ = std::make_unique<gfx::ShaderManager>();
  shader_mgr_->init(
      device_, gfx::ShaderManager::Options{.targets = device_->get_supported_shader_targets()});
  clear_color_cmp_pso_ = device_->create_compute_pipeline_h({"vulkan_exp/clear_tex_to_color"});
  test_gfx_pso_ = device_->create_graphics_pipeline_h({
      .shaders = {{"fullscreen_quad", rhi::ShaderType::Vertex},
                  {"vulkan_exp/single_tex", rhi::ShaderType::Fragment}},
  });
  test_geo_pso_ = device_->create_graphics_pipeline_h({
      .shaders = {{"vulkan_exp/basic_geo", rhi::ShaderType::Vertex},
                  {"vulkan_exp/single_color", rhi::ShaderType::Fragment}},
  });
  test_vert_buf_ = device_->create_buf_h({
      .usage = rhi::BufferUsage::Storage,
      .size = 1024ul * 1024,
  });
  recreate_resources_on_swapchain_resize();

  std::vector<DefaultVertex> tri_verts;
  tri_verts.emplace_back(glm::vec4{-.5f, -0.5f, 0.0f, 1.f}, glm::vec2{0.f, 0.f});
  tri_verts.emplace_back(glm::vec4{.5f, -0.5f, 0.0f, 1.f}, glm::vec2{1.f, 0.f});
  tri_verts.emplace_back(glm::vec4{0.0f, .5f, 0.0f, 1.f}, glm::vec2{0.5f, 1.f});
  buffer_copy_mgr_.copy_to_buffer(tri_verts.data(), tri_verts.size() * sizeof(DefaultVertex),
                                  test_vert_buf_.handle, 0, PipelineStage::VertexShader,
                                  AccessFlags::ShaderRead);
  rg_.init(device_);
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

TestRenderer::~TestRenderer() { shader_mgr_->shutdown(); }

void TestRenderer::recreate_resources_on_swapchain_resize() {
  auto dims = glm::uvec2{swapchain_->desc_.width, swapchain_->desc_.height};
  test_full_screen_tex_ = device_->create_tex_h({
      .format = rhi::TextureFormat::R32G32B32A32Sfloat,
      .usage =
          rhi::TextureUsage::Sample | rhi::TextureUsage::Storage | rhi::TextureUsage::ShaderWrite,
      .dims = {dims.x, dims.y, 1},
      .mip_levels = 1,
      .array_length = 1,
      .name = "test full screen texture",
  });
}

void TestRenderer::add_render_graph_passes() {
  {
    auto& p = rg_.add_compute_pass("compute_clear_pass");
    p.w_external_tex("test_full_screen_tex_", test_full_screen_tex_.handle);
    p.set_ex([this](rhi::CmdEncoder* enc) {
      auto tex_handle = test_full_screen_tex_.handle;

      auto* tex = device_->get_tex(tex_handle);
      enc->bind_pipeline(clear_color_cmp_pso_);
      struct {
        glm::uvec2 dims;
      } pc;
      pc.dims = tex->desc().dims;
      enc->push_constants(&pc, sizeof(pc));
      enc->bind_uav(tex_handle, 0);
      enc->dispatch_compute(
          glm::uvec3{align_divide_up(pc.dims.x, 8), align_divide_up(pc.dims.y, 8), 1},
          glm::uvec3{8, 8, 1});
    });
  }
  {
    auto& p = rg_.add_graphics_pass("fullscreen");
    p.r_external_tex("test_full_screen_tex_", rhi::PipelineStage::FragmentShader);
    p.w_swapchain_tex(swapchain_);
    p.set_ex([this](rhi::CmdEncoder* enc) {
      glm::vec4 clear_color{0.5, 0.5, 0, 1};
      device_->begin_swapchain_rendering(swapchain_, enc, &clear_color);
      enc->set_cull_mode(rhi::CullMode::None);
      enc->set_wind_order(rhi::WindOrder::CounterClockwise);
      enc->set_viewport({0, 0}, {swapchain_->desc_.width, swapchain_->desc_.height});
      enc->set_scissor({0, 0}, {swapchain_->desc_.width, swapchain_->desc_.height});
      enc->bind_pipeline(test_gfx_pso_);
      enc->bind_srv(test_full_screen_tex_.handle, 0);
      enc->draw_primitives(rhi::PrimitiveTopology::TriangleList, 0, 3);

      enc->bind_pipeline(test_geo_pso_);
      enc->bind_srv(test_vert_buf_.handle, 0);
      enc->draw_primitives(rhi::PrimitiveTopology::TriangleList, 0, 3);
      enc->end_rendering();
    });
  }
}

}  // namespace teng::gfx
