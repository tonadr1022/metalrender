#include "TestDebugScenes.hpp"

#include "core/EAssert.hpp"
#include "core/Util.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/Pipeline.hpp"
#include "gfx/rhi/Swapchain.hpp"
#include "gfx/rhi/Texture.hpp"
#include "hlsl/default_vertex.h"

using namespace teng;
using namespace teng::gfx;
using namespace teng::gfx::rhi;

namespace teng::gfx {

namespace {

class ComputePlusVertexScene final : public ITestScene {
 public:
  void init(const TestSceneContext& ctx) override {
    auto* device = ctx.device;
    clear_color_cmp_pso_ = device->create_compute_pipeline_h({"vulkan_exp/clear_tex_to_color"});
    test_gfx_pso_ = device->create_graphics_pipeline_h({
        .shaders = {{"fullscreen_quad", ShaderType::Vertex},
                    {"vulkan_exp/single_tex", ShaderType::Fragment}},
    });
    test_geo_pso_ = device->create_graphics_pipeline_h({
        .shaders = {{"vulkan_exp/basic_geo", ShaderType::Vertex},
                    {"vulkan_exp/single_color", ShaderType::Fragment}},
    });
    test_vert_buf_ = device->create_buf_h({
        .usage = BufferUsage::Storage,
        .size = 1024ul * 1024,
        .flags = BufferDescFlags::CPUAccessible,
    });
    std::vector<DefaultVertex> tri_verts;
    tri_verts.emplace_back(glm::vec4{-.5f, -0.5f, 0.0f, 1.f}, glm::vec2{0.f, 0.f});
    tri_verts.emplace_back(glm::vec4{.5f, -0.5f, 0.0f, 1.f}, glm::vec2{1.f, 0.f});
    tri_verts.emplace_back(glm::vec4{0.0f, .5f, 0.0f, 1.f}, glm::vec2{0.5f, 1.f});
    ctx.buffer_copy->copy_to_buffer(tri_verts.data(), tri_verts.size() * sizeof(DefaultVertex),
                                    test_vert_buf_.handle, 0, PipelineStage::VertexShader,
                                    AccessFlags::ShaderRead);
    on_swapchain_resize(ctx);
  }

  void shutdown() override {
    clear_color_cmp_pso_ = {};
    test_gfx_pso_ = {};
    test_geo_pso_ = {};
    test_full_screen_tex_ = {};
    test_vert_buf_ = {};
  }

  void on_swapchain_resize(const TestSceneContext& ctx) override {
    auto dims = glm::uvec2{ctx.swapchain->desc_.width, ctx.swapchain->desc_.height};
    test_full_screen_tex_ = ctx.device->create_tex_h({
        .format = TextureFormat::R32G32B32A32Sfloat,
        .usage = TextureUsage::Sample | TextureUsage::Storage | TextureUsage::ShaderWrite,
        .dims = {dims.x, dims.y, 1},
        .mip_levels = 1,
        .array_length = 1,
        .name = "test full screen texture",
    });
  }

  void add_render_graph_passes(const TestSceneContext& ctx) override {
    auto test_full_screen_tex_id =
        ctx.rg->import_external_texture(test_full_screen_tex_.handle, "test_full_screen_tex");
    {
      auto& p = ctx.rg->add_compute_pass("compute_clear_pass");
      p.write_tex(test_full_screen_tex_id, PipelineStage::ComputeShader);
      p.set_ex([this, ctx](CmdEncoder* enc) {
        auto tex_handle = test_full_screen_tex_.handle;
        auto* tex = ctx.device->get_tex(tex_handle);
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
      auto& p = ctx.rg->add_graphics_pass("fullscreen");
      p.sample_tex(test_full_screen_tex_id);
      p.w_swapchain_tex(ctx.swapchain);
      p.set_ex([this, ctx](CmdEncoder* enc) {
        glm::vec4 clear_color{0.5, 0.5, 0, 1};
        ctx.device->begin_swapchain_rendering(ctx.swapchain, enc, &clear_color);
        enc->set_cull_mode(CullMode::None);
        enc->set_wind_order(WindOrder::CounterClockwise);
        enc->set_viewport({0, 0}, {ctx.swapchain->desc_.width, ctx.swapchain->desc_.height});
        enc->set_scissor({0, 0}, {ctx.swapchain->desc_.width, ctx.swapchain->desc_.height});
        enc->bind_pipeline(test_gfx_pso_);
        enc->bind_srv(test_full_screen_tex_.handle, 0);
        enc->draw_primitives(PrimitiveTopology::TriangleList, 0, 3);

        enc->bind_pipeline(test_geo_pso_);
        enc->bind_srv(test_vert_buf_.handle, 0);
        enc->draw_primitives(PrimitiveTopology::TriangleList, 0, 3);
        enc->end_rendering();
      });
    }
  }

 private:
  PipelineHandleHolder clear_color_cmp_pso_;
  PipelineHandleHolder test_gfx_pso_;
  PipelineHandleHolder test_geo_pso_;
  TextureHandleHolder test_full_screen_tex_;
  BufferHandleHolder test_vert_buf_;
};

class MeshHelloTriangleScene final : public ITestScene {
 public:
  void init(const TestSceneContext& ctx) override {
    mesh_pso_ = ctx.device->create_graphics_pipeline_h({
        .shaders = {{"test_mesh", ShaderType::Mesh}, {"test_mesh", ShaderType::Fragment}},
    });
  }

  void shutdown() override { mesh_pso_ = {}; }

  void on_swapchain_resize(const TestSceneContext&) override {}

  void add_render_graph_passes(const TestSceneContext& ctx) override {
    auto& p = ctx.rg->add_graphics_pass("mesh_hello");
    p.w_swapchain_tex(ctx.swapchain);
    p.set_ex([this, ctx](CmdEncoder* enc) {
      glm::vec4 clear_color{0.1f, 0.1f, 0.15f, 1.f};
      ctx.device->begin_swapchain_rendering(ctx.swapchain, enc, &clear_color);
      enc->set_cull_mode(CullMode::None);
      enc->set_wind_order(WindOrder::CounterClockwise);
      enc->set_viewport({0, 0}, {ctx.swapchain->desc_.width, ctx.swapchain->desc_.height});
      enc->set_scissor({0, 0}, {ctx.swapchain->desc_.width, ctx.swapchain->desc_.height});
      enc->bind_pipeline(mesh_pso_);
      enc->draw_mesh_threadgroups({1, 1, 1}, {1, 1, 1}, {128, 1, 1});
      enc->end_rendering();
    });
  }

 private:
  PipelineHandleHolder mesh_pso_;
};

}  // namespace

const char* to_string(TestDebugScene s) {
  switch (s) {
    case TestDebugScene::ComputePlusVertexOverlay:
      return "ComputePlusVertexOverlay";
    case TestDebugScene::MeshHelloTriangle:
      return "MeshHelloTriangle";
    case TestDebugScene::Count:
    default:
      return "Invalid";
  }
}

std::unique_ptr<ITestScene> create_test_scene(TestDebugScene s) {
  switch (s) {
    case TestDebugScene::ComputePlusVertexOverlay:
      return std::make_unique<ComputePlusVertexScene>();
    case TestDebugScene::MeshHelloTriangle:
      return std::make_unique<MeshHelloTriangleScene>();
    case TestDebugScene::Count:
    default:
      ASSERT(0);
      return nullptr;
  }
}

}  // namespace teng::gfx
