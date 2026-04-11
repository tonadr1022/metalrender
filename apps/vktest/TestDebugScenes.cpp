#include "TestDebugScenes.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

#include "Camera.hpp"
#include "core/EAssert.hpp"
#include "core/Util.hpp"
#include "gfx/GPUFrameAllocator2.hpp"
#include "gfx/ImGuiRenderer.hpp"
#include "gfx/ModelGPUManager.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/renderer/InstanceMgr.hpp"
#include "gfx/rhi/Buffer.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "gfx/rhi/Pipeline.hpp"
#include "gfx/rhi/Swapchain.hpp"
#include "gfx/rhi/Texture.hpp"
#include "hlsl/default_vertex.h"
#include "scenes/MeshletRendererTestScene.hpp"

using namespace teng;
using namespace teng::gfx;
using namespace teng::gfx::rhi;

namespace teng::gfx {

namespace {

class ComputePlusVertexScene final : public ITestScene {
 public:
  explicit ComputePlusVertexScene(const TestSceneContext& ctx) : ITestScene(ctx) {
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
    ctx_.buffer_copy->copy_to_buffer(tri_verts.data(), tri_verts.size() * sizeof(DefaultVertex),
                                     test_vert_buf_.handle, 0, PipelineStage::VertexShader,
                                     AccessFlags::ShaderRead);
    on_swapchain_resize();
  }

  void on_swapchain_resize() override {
    auto dims = glm::uvec2{ctx_.swapchain->desc_.width, ctx_.swapchain->desc_.height};
    test_full_screen_tex_ = ctx_.device->create_tex_h({
        .format = TextureFormat::R32G32B32A32Sfloat,
        .usage = TextureUsage::Sample | TextureUsage::Storage | TextureUsage::ShaderWrite,
        .dims = {dims.x, dims.y, 1},
        .mip_levels = 1,
        .array_length = 1,
        .name = "test full screen texture",
    });
  }

  void add_render_graph_passes() override {
    auto test_full_screen_tex_id =
        ctx_.rg->import_external_texture(test_full_screen_tex_.handle, "test_full_screen_tex");
    {
      auto& p = ctx_.rg->add_compute_pass("compute_clear_pass");
      p.write_tex(test_full_screen_tex_id, PipelineStage::ComputeShader);
      p.set_ex([this](CmdEncoder* enc) {
        auto tex_handle = test_full_screen_tex_.handle;
        auto* tex = ctx_.device->get_tex(tex_handle);
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
      auto& p = ctx_.rg->add_graphics_pass("fullscreen");
      p.sample_tex(test_full_screen_tex_id);
      p.w_swapchain_tex(ctx_.swapchain);
      p.set_ex([this](CmdEncoder* enc) {
        glm::vec4 clear_color{0.5, 0.5, 0, 1};
        ctx_.device->begin_swapchain_rendering(ctx_.swapchain, enc, &clear_color);
        enc->set_cull_mode(CullMode::None);
        enc->set_wind_order(WindOrder::CounterClockwise);
        enc->set_viewport({0, 0}, {ctx_.swapchain->desc_.width, ctx_.swapchain->desc_.height});
        enc->set_scissor({0, 0}, {ctx_.swapchain->desc_.width, ctx_.swapchain->desc_.height});
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

struct MeshHelloVertex {
  glm::vec4 pos;
  glm::vec4 color;
};

struct CubeVertex {
  glm::vec4 pos;
  glm::vec2 uv;
};
static_assert(sizeof(CubeVertex) == 24);

struct CubePush {
  glm::mat4 mvp;
  uint32_t vert_buf_idx;
  uint32_t tex_idx;
};
static_assert(sizeof(CubePush) == 72);

[[nodiscard]] std::vector<CubeVertex> make_textured_cube_vertices() {
  std::vector<CubeVertex> v;
  v.reserve(24);
  const float h = 0.5f;
  // +Z
  v.push_back({{-h, -h, h, 1.f}, {0.f, 0.f}});
  v.push_back({{h, -h, h, 1.f}, {1.f, 0.f}});
  v.push_back({{h, h, h, 1.f}, {1.f, 1.f}});
  v.push_back({{-h, h, h, 1.f}, {0.f, 1.f}});
  // -Z
  v.push_back({{h, -h, -h, 1.f}, {0.f, 0.f}});
  v.push_back({{-h, -h, -h, 1.f}, {1.f, 0.f}});
  v.push_back({{-h, h, -h, 1.f}, {1.f, 1.f}});
  v.push_back({{h, h, -h, 1.f}, {0.f, 1.f}});
  // +X
  v.push_back({{h, -h, h, 1.f}, {0.f, 0.f}});
  v.push_back({{h, -h, -h, 1.f}, {1.f, 0.f}});
  v.push_back({{h, h, -h, 1.f}, {1.f, 1.f}});
  v.push_back({{h, h, h, 1.f}, {0.f, 1.f}});
  // -X
  v.push_back({{-h, -h, -h, 1.f}, {0.f, 0.f}});
  v.push_back({{-h, -h, h, 1.f}, {1.f, 0.f}});
  v.push_back({{-h, h, h, 1.f}, {1.f, 1.f}});
  v.push_back({{-h, h, -h, 1.f}, {0.f, 1.f}});
  // +Y
  v.push_back({{-h, h, h, 1.f}, {0.f, 0.f}});
  v.push_back({{h, h, h, 1.f}, {1.f, 0.f}});
  v.push_back({{h, h, -h, 1.f}, {1.f, 1.f}});
  v.push_back({{-h, h, -h, 1.f}, {0.f, 1.f}});
  // -Y
  v.push_back({{-h, -h, -h, 1.f}, {0.f, 0.f}});
  v.push_back({{h, -h, -h, 1.f}, {1.f, 0.f}});
  v.push_back({{h, -h, h, 1.f}, {1.f, 1.f}});
  v.push_back({{-h, -h, h, 1.f}, {0.f, 1.f}});
  return v;
}

[[nodiscard]] std::vector<uint32_t> make_checker_rgba(uint32_t dim, uint32_t cell_px) {
  std::vector<uint32_t> px((size_t)dim * dim);
  for (uint32_t y = 0; y < dim; y++) {
    for (uint32_t x = 0; x < dim; x++) {
      bool white = (((x / cell_px) + (y / cell_px)) & 1u) == 0;
      px[y * dim + x] = white ? 0xFFFFFFFFu : 0xFF202020u;
    }
  }
  return px;
}

class MeshHelloTriangleScene final : public ITestScene {
 public:
  explicit MeshHelloTriangleScene(const TestSceneContext& ctx) : ITestScene(ctx) {
    mesh_pso_ = ctx.device->create_graphics_pipeline_h({
        .shaders = {{"test_mesh_buf", ShaderType::Mesh}, {"test_mesh_buf", ShaderType::Fragment}},
    });
    mesh_vert_buf_ = ctx.device->create_buf_h({
        .usage = BufferUsage::Storage,
        .size = 1024ul * 1024,
        .flags = BufferDescFlags::CPUAccessible,
    });
    std::vector<MeshHelloVertex> tri;
    tri.push_back({glm::vec4{-0.5f, -0.5f, 0.0f, 1.f}, glm::vec4{1.f, 0.f, 0.f, 1.f}});
    tri.push_back({glm::vec4{0.5f, -0.5f, 0.0f, 1.f}, glm::vec4{0.f, 0.f, 1.f, 1.f}});
    tri.push_back({glm::vec4{0.0f, 0.5f, 0.0f, 1.f}, glm::vec4{0.f, 1.f, 0.f, 1.f}});
    ctx_.buffer_copy->copy_to_buffer(tri.data(), tri.size() * sizeof(MeshHelloVertex),
                                     mesh_vert_buf_.handle, 0, PipelineStage::MeshShader,
                                     AccessFlags::ShaderRead);
  }

  void on_swapchain_resize() override {}

  void add_render_graph_passes() override {
    auto& p = ctx_.rg->add_graphics_pass("mesh_hello");
    p.w_swapchain_tex(ctx_.swapchain);
    p.set_ex([this](CmdEncoder* enc) {
      glm::vec4 clear_color{0.1f, 0.1f, 0.15f, 1.f};
      ctx_.device->begin_swapchain_rendering(ctx_.swapchain, enc, &clear_color);
      enc->set_cull_mode(CullMode::None);
      enc->set_wind_order(WindOrder::CounterClockwise);
      enc->set_viewport({0, 0}, {ctx_.swapchain->desc_.width, ctx_.swapchain->desc_.height});
      enc->set_scissor({0, 0}, {ctx_.swapchain->desc_.width, ctx_.swapchain->desc_.height});
      enc->bind_pipeline(mesh_pso_);
      enc->bind_srv(mesh_vert_buf_.handle, 0);
      enc->draw_mesh_threadgroups({1, 1, 1}, {1, 1, 1}, {128, 1, 1});
      enc->end_rendering();
    });
  }

 private:
  PipelineHandleHolder mesh_pso_;
  BufferHandleHolder mesh_vert_buf_;
};

class TexturedCubeProceduralScene final : public ITestScene {
 public:
  explicit TexturedCubeProceduralScene(const TestSceneContext& ctx) : ITestScene(ctx) {
    auto* device = ctx.device;
    mesh_pso_ = device->create_graphics_pipeline_h({
        .shaders = {{"test_cube_bindless", ShaderType::Mesh},
                    {"test_cube_bindless", ShaderType::Fragment}},
    });
    auto verts = make_textured_cube_vertices();
    cube_vert_buf_ = device->create_buf_h({
        .usage = BufferUsage::Storage,
        .size = std::max(1024ul * 1024, verts.size() * sizeof(CubeVertex)),
        .flags = BufferDescFlags::CPUAccessible,
        .name = "textured_cube_verts",
    });
    ctx_.buffer_copy->copy_to_buffer(verts.data(), verts.size() * sizeof(CubeVertex),
                                     cube_vert_buf_.handle, 0, PipelineStage::MeshShader,
                                     AccessFlags::ShaderRead);

    constexpr uint32_t k_checker_dim = 256;
    constexpr uint32_t k_cell = 32;
    checker_tex_data_ = make_checker_rgba(k_checker_dim, k_cell);
    checker_dim_ = k_checker_dim;
    checker_tex_ = device->create_tex_h({
        .format = TextureFormat::R8G8B8A8Unorm,
        .usage = TextureUsage::Sample,
        .dims = {checker_dim_, checker_dim_, 1},
        .mip_levels = 1,
        .array_length = 1,
        .name = "textured_cube_checker",
    });
    vert_bindless_ = device->get_buf(cube_vert_buf_.handle)->bindless_idx();
    tex_bindless_ = device->get_tex(checker_tex_.handle)->bindless_idx();

    camera_.pos = {0.f, 0.f, 4.f};
    camera_.pitch = 0.f;
    camera_.yaw = -90.f;
    camera_.calc_vectors();
    checker_upload_done_ = false;
  }

  void on_swapchain_resize() override {}

  void add_render_graph_passes() override {
    // Bindless mesh + texture are not declared as RG external reads: the graph requires every
    // external read to have a producer pass, while these resources are filled outside the graph
    // (buffer upload in init, texture via upload_texture_data here). Same idea as MeshHelloTriangle
    // (no import/read_buf for the mesh vertex buffer).
    auto& p = ctx_.rg->add_graphics_pass("textured_cube_mesh");
    p.w_swapchain_tex(ctx_.swapchain);
    p.set_ex([this](CmdEncoder* enc) {
      if (!checker_upload_done_ && ctx_.frame_staging != nullptr) {
        const uint32_t w = checker_dim_;
        const uint32_t h = checker_dim_;
        const size_t src_bpr = static_cast<size_t>(w) * 4u;
        const auto dst_bpr = static_cast<size_t>(align_up(static_cast<uint64_t>(src_bpr), 256));
        auto upload = ctx_.frame_staging->alloc(static_cast<uint32_t>(dst_bpr * h));
        for (uint32_t row = 0; row < h; row++) {
          std::memcpy(static_cast<std::byte*>(upload.write_ptr) + row * dst_bpr,
                      reinterpret_cast<const std::byte*>(checker_tex_data_.data()) + row * src_bpr,
                      src_bpr);
        }
        enc->upload_texture_data(upload.buf, upload.offset, dst_bpr, checker_tex_.handle,
                                 glm::uvec3{w, h, 1}, glm::uvec3{0, 0, 0}, 0);
        checker_upload_done_ = true;
      }

      glm::vec4 clear_color{0.08f, 0.08f, 0.1f, 1.f};
      ctx_.device->begin_swapchain_rendering(ctx_.swapchain, enc, &clear_color);
      enc->bind_pipeline(mesh_pso_);
      enc->set_cull_mode(CullMode::Back);
      enc->set_wind_order(WindOrder::CounterClockwise);
      enc->set_viewport({0, 0}, {ctx_.swapchain->desc_.width, ctx_.swapchain->desc_.height});
      enc->set_scissor({0, 0}, {ctx_.swapchain->desc_.width, ctx_.swapchain->desc_.height});

      const float aspect = static_cast<float>(ctx_.swapchain->desc_.width) /
                           std::max(1.f, static_cast<float>(ctx_.swapchain->desc_.height));
      const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(60.f), aspect, 0.1f, 100.f);
      camera_.calc_vectors();
      const glm::mat4 view = camera_.get_view_mat();
      const float t = ctx_.time_sec;
      const glm::mat4 model = glm::rotate(glm::mat4(1.f), t, glm::vec3(0.f, 1.f, 0.f)) *
                              glm::rotate(glm::mat4(1.f), t * 0.73f, glm::vec3(1.f, 0.f, 0.f));
      const glm::mat4 mvp = proj * view * model;

      CubePush pc{};
      pc.mvp = mvp;
      pc.vert_buf_idx = vert_bindless_;
      pc.tex_idx = tex_bindless_;
      enc->push_constants(&pc, sizeof(pc));

      enc->draw_mesh_threadgroups({1, 1, 1}, {1, 1, 1}, {128, 1, 1});

      ctx_.imgui_renderer->render(enc, {ctx_.swapchain->desc_.width, ctx_.swapchain->desc_.height},
                                  ctx_.curr_frame_in_flight_idx);

      enc->end_rendering();
    });
  }

 private:
  PipelineHandleHolder mesh_pso_;
  BufferHandleHolder cube_vert_buf_;
  TextureHandleHolder checker_tex_;
  Camera camera_;
  uint32_t vert_bindless_{};
  uint32_t tex_bindless_{};
  uint32_t checker_dim_{};
  std::vector<uint32_t> checker_tex_data_;
  bool checker_upload_done_{};
};

}  // namespace

const char* to_string(TestDebugScene s) {
  switch (s) {
    case TestDebugScene::ComputePlusVertexOverlay:
      return "ComputePlusVertexOverlay";
    case TestDebugScene::MeshHelloTriangle:
      return "MeshHelloTriangle";
    case TestDebugScene::TexturedCubeProcedural:
      return "TexturedCubeProcedural";
    case TestDebugScene::MeshletRenderer:
      return "MeshletRenderer";
    case TestDebugScene::Count:
    default:
      return "Invalid";
  }
}

std::unique_ptr<ITestScene> create_test_scene(TestDebugScene s, const TestSceneContext& ctx) {
  switch (s) {
    case TestDebugScene::ComputePlusVertexOverlay:
      return std::make_unique<ComputePlusVertexScene>(ctx);
    case TestDebugScene::MeshHelloTriangle:
      return std::make_unique<MeshHelloTriangleScene>(ctx);
    case TestDebugScene::TexturedCubeProcedural:
      return std::make_unique<TexturedCubeProceduralScene>(ctx);
    case TestDebugScene::MeshletRenderer:
      return std::make_unique<MeshletRendererScene>(ctx);
    case TestDebugScene::Count:
    default:
      ASSERT(0);
      return nullptr;
  }
}

}  // namespace teng::gfx
