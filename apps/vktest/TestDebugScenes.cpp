#include "TestDebugScenes.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

#include "Camera.hpp"
#include "FpsCameraController.hpp"
#include "ResourceManager.hpp"
#include "Window.hpp"
#include "core/EAssert.hpp"
#include "core/Util.hpp"
#include "gfx/GPUFrameAllocator2.hpp"
#include "gfx/ImGuiRenderer.hpp"
#include "gfx/ModelGPUManager.hpp"
#include "gfx/ModelLoader.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/ShaderManager.hpp"
#include "gfx/renderer/InstanceMgr.hpp"
#include "gfx/rhi/Buffer.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/Config.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "gfx/rhi/Pipeline.hpp"
#include "gfx/rhi/Swapchain.hpp"
#include "gfx/rhi/Texture.hpp"
#include "hlsl/default_vertex.h"
#include "hlsl/shader_constants.h"
#include "hlsl/shared_cull_data.h"
#include "hlsl/shared_debug_meshlet_prepare.h"
#include "hlsl/shared_forward_meshlet.h"
#include "hlsl/shared_globals.h"
#include "hlsl/shared_task_cmd.h"
#include "hlsl/shared_test_clear_buf.h"
#include "imgui.h"
#include "ktx.h"

using namespace teng;
using namespace teng::gfx;
using namespace teng::gfx::rhi;

namespace teng::gfx {

namespace {

[[maybe_unused]] constexpr const char* sponza_path = "Models/Sponza/glTF/Sponza.gltf";
[[maybe_unused]] constexpr const char* chessboard_path =
    "Models/ABeautifulGame/glTF_ktx2/ABeautifulGame.gltf";
[[maybe_unused]] constexpr const char* suzanne_path = "Models/Suzanne/glTF/Suzanne.gltf";
[[maybe_unused]] constexpr const char* cube_path = "Models/Cube/glTF/Cube.gltf";

std::filesystem::path resolve_model_path(const std::filesystem::path& resource_dir,
                                         const std::string& path) {
  if (path.starts_with("Models")) {
    return resource_dir / "models" / "gltf" / path;
  }
  return path;
}

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

void flush_pending_model_textures(ModelGPUMgr& mgr, rhi::Device& device,
                                  GPUFrameAllocator3& staging, rhi::CmdEncoder* enc) {
  const auto& pending = mgr.get_pending_texture_uploads();
  if (pending.empty()) {
    return;
  }
  for (const auto& upload : pending) {
    const auto& tex_upload = upload.upload;
    auto* tex = device.get_tex(upload.tex);
    ASSERT(tex);
    ASSERT(tex_upload.data);
    if (tex_upload.load_type == CPUTextureLoadType::Ktx2) {
      auto* ktx_tex = (ktxTexture2*)tex_upload.data.get();
      const auto& desc = upload.upload.desc;
      size_t block_width = get_block_width_bytes(desc.format);
      size_t bytes_per_block = get_bytes_per_block(desc.format);
      size_t total_img_size = 0;
      for (uint32_t mip_level = 0; mip_level < desc.mip_levels; mip_level++) {
        total_img_size += ktxTexture_GetImageSize(ktxTexture(ktx_tex), mip_level);
      }
      auto upload_buf = staging.alloc(static_cast<uint32_t>(total_img_size));
      ASSERT(upload_buf.buf.is_valid());
      size_t curr_dst_offset = 0;
      for (uint32_t mip_level = 0; mip_level < desc.mip_levels; mip_level++) {
        size_t offset = 0;
        auto result = ktxTexture_GetImageOffset(ktxTexture(ktx_tex), mip_level, 0, 0, &offset);
        ASSERT(result == KTX_SUCCESS);
        auto img_mip_level_size_bytes = ktxTexture_GetImageSize(ktxTexture(ktx_tex), mip_level);
        uint32_t mip_width = std::max(1u, desc.dims.x >> mip_level);
        uint32_t mip_height = std::max(1u, desc.dims.y >> mip_level);
        uint32_t blocks_wide = align_divide_up(mip_width, static_cast<uint32_t>(block_width));
        auto bpr = static_cast<size_t>(blocks_wide) * bytes_per_block;
        std::memcpy(static_cast<std::byte*>(upload_buf.write_ptr) + curr_dst_offset,
                    reinterpret_cast<const std::byte*>(ktx_tex->pData) + offset,
                    img_mip_level_size_bytes);
        enc->upload_texture_data(
            upload_buf.buf, upload_buf.offset + static_cast<uint32_t>(curr_dst_offset), bpr,
            upload.tex, glm::uvec3{mip_width, mip_height, 1}, glm::uvec3{0, 0, 0}, mip_level);
        curr_dst_offset += img_mip_level_size_bytes;
      }
    } else {
      size_t src_bytes_per_row = tex_upload.bytes_per_row;
      size_t bytes_per_row = align_up(src_bytes_per_row, 256);
      size_t total_size = bytes_per_row * tex->desc().dims.y;
      auto upload_buf = staging.alloc(static_cast<uint32_t>(total_size));
      size_t dst_offset = 0;
      size_t src_offset = 0;
      for (size_t row = 0; row < tex->desc().dims.y; row++) {
        std::memcpy(static_cast<std::byte*>(upload_buf.write_ptr) + dst_offset,
                    static_cast<const std::byte*>(tex_upload.data.get()) + src_offset,
                    src_bytes_per_row);
        dst_offset += bytes_per_row;
        src_offset += src_bytes_per_row;
      }
      enc->upload_texture_data(upload_buf.buf, upload_buf.offset, bytes_per_row, upload.tex,
                               glm::uvec3{tex->desc().dims.x, tex->desc().dims.y, 1},
                               glm::uvec3{0, 0, 0}, 0);
    }
  }
  mgr.clear_pending_texture_uploads();
}

class MeshletRendererScene final : public ITestScene {
 public:
  explicit MeshletRendererScene(const TestSceneContext& ctx) : ITestScene(ctx) {
    ASSERT(ctx_.model_gpu_mgr != nullptr);
    ASSERT(ctx_.shader_mgr != nullptr);
    ASSERT(ctx_.frame_staging != nullptr);

    test_model_handle_ = ResourceManager::get().load_model(
        resolve_model_path(ctx_.resource_dir, sponza_path), glm::mat4{1.f});
    ModelInstance* inst = ResourceManager::get().get_model(test_model_handle_);
    ASSERT(inst);
    auto alloc_opt = ctx_.model_gpu_mgr->instance_alloc(inst->instance_gpu_handle);
    ASSERT(alloc_opt.has_value());
    instance_alloc_ = *alloc_opt;
    const ModelGPUResources* res = ctx_.model_gpu_mgr->model_resources(inst->model_gpu_handle);
    ASSERT(res);
    ASSERT(res->totals.task_cmd_count == ctx_.model_gpu_mgr->geometry_batch().task_cmd_count);

    constexpr size_t k_ubo_align = 256;
    view_cb_buf_ = ctx_.device->create_buf_h({
        .usage = BufferUsage::Uniform,
        .size = align_up(sizeof(ViewData), k_ubo_align),
        .flags = BufferDescFlags::CPUAccessible,
        .name = "meshlet_hello_view",
    });
    view_prepare_storage_buf_ = ctx_.device->create_buf_h({
        .usage = BufferUsage::Storage,
        .size = align_up(sizeof(ViewData), k_ubo_align),
        .flags = BufferDescFlags::CPUAccessible,
        .name = "meshlet_prepare_view_storage",
    });
    globals_cb_buf_ = ctx_.device->create_buf_h({
        .usage = BufferUsage::Uniform,
        .size = align_up(sizeof(GlobalData), k_ubo_align),
        .flags = BufferDescFlags::CPUAccessible,
        .name = "meshlet_hello_globals",
    });
    cull_storage_buf_ = ctx_.device->create_buf_h({
        .usage = BufferUsage::Storage,
        .size = align_up(sizeof(CullData), k_ubo_align),
        .flags = BufferDescFlags::CPUAccessible,
        .name = "meshlet_prepare_cull_storage",
    });

    clear_indirect_pso_ = ctx_.shader_mgr->create_compute_pipeline(
        {.path = "test_clear_cnt_buf", .type = ShaderType::Compute});
    prepare_meshlets_pso_ = ctx_.shader_mgr->create_compute_pipeline(
        {.path = "debug_meshlet_prepare_meshlets", .type = ShaderType::Compute});

    for (int i = 0; i < k_max_frames_in_flight; i++) {
      draw_count_readback_[static_cast<size_t>(i)] = ctx_.device->create_buf_h({
          .size = sizeof(uint32_t),
          .flags = BufferDescFlags::CPUAccessible,
          .name = "meshlet_draw_count_readback",
      });
      visible_object_count_readback_[static_cast<size_t>(i)] = ctx_.device->create_buf_h({
          .size = sizeof(uint32_t),
          .flags = BufferDescFlags::CPUAccessible,
          .name = "meshlet_visible_object_count_readback",
      });
    }

    fps_camera_.camera().pos = {0.f, 0.f, 3.f};
    fps_camera_.camera().pitch = 0.f;
    fps_camera_.camera().yaw = -90.f;
    fps_camera_.camera().calc_vectors();

    recreate_meshlet_depth_tex();
    recreate_meshlet_pso();
  }

  void shutdown() override {
    if (ctx_.window) {
      fps_camera_.set_mouse_captured(ctx_.window->get_handle(), false);
    }
    ResourceManager::get().free_model(test_model_handle_);
  }

  void on_frame(const TestSceneContext& ctx) override {
    const bool imgui_blocks =
        ctx.imgui_ui_active && (ImGui::GetIO().WantTextInput || ImGui::GetIO().WantCaptureKeyboard);
    if (ctx.window) {
      fps_camera_.update(ctx.window->get_handle(), ctx.delta_time_sec, imgui_blocks);
    }
  }

  void on_cursor_pos(double x, double y) override { fps_camera_.on_cursor_pos(x, y); }

  void on_key_event(int key, int action, int mods) override {
    (void)mods;
    if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE && ctx_.window) {
      fps_camera_.toggle_mouse_capture(ctx_.window->get_handle());
    }
  }

  void on_imgui() override {
    ImGui::Begin("Meshlet renderer");
    ImGui::Checkbox("GPU object frustum cull", &gpu_object_frustum_cull_);
    uint32_t visible_meshlet_task_groups = 0;
    uint32_t visible_objects = 0;
    if (meshlet_readback_frames_ >= ctx_.device->get_info().frames_in_flight) {
      const uint32_t curr = ctx_.curr_frame_in_flight_idx;
      visible_meshlet_task_groups = *reinterpret_cast<const uint32_t*>(
          ctx_.device->get_buf(draw_count_readback_[curr].handle)->contents());
      visible_objects = *reinterpret_cast<const uint32_t*>(
          ctx_.device->get_buf(visible_object_count_readback_[curr].handle)->contents());
    }
    ImGui::Text("Visible mesh task groups (GPU): %u", visible_meshlet_task_groups);
    ImGui::Text("Visible objects (GPU): %u", visible_objects);
    ImGui::End();
  }

  void on_swapchain_resize() override {
    recreate_meshlet_depth_tex();
    recreate_meshlet_pso();
  }

  void add_render_graph_passes() override {
    auto& batch = ctx_.model_gpu_mgr->geometry_batch();
    const uint32_t tc = batch.task_cmd_count;
    if (tc == 0 || batch.get_stats().vertex_count == 0) {
      return;
    }

    meshlet_readback_frames_++;

    const float aspect = static_cast<float>(ctx_.swapchain->desc_.width) /
                         std::max(1.f, static_cast<float>(ctx_.swapchain->desc_.height));
    glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(60.f), aspect, 0.1f, 100.f);
    proj[1][1] = -proj[1][1];
    fps_camera_.camera().calc_vectors();
    const glm::mat4 view = fps_camera_.camera().get_view_mat();
    const glm::mat4 vp = proj * view;

    ViewData vd{};
    vd.vp = vp;
    vd.inv_vp = glm::inverse(vp);
    vd.view = view;
    vd.proj = proj;
    vd.inv_proj = glm::inverse(proj);
    vd.camera_pos = glm::vec4(fps_camera_.camera().pos, 1.f);
    std::memcpy(ctx_.device->get_buf(view_cb_buf_.handle)->contents(), &vd, sizeof(vd));
    std::memcpy(ctx_.device->get_buf(view_prepare_storage_buf_.handle)->contents(), &vd,
                sizeof(vd));

    const auto normalize_plane = [](const glm::vec4& plane) {
      const glm::vec3 n = glm::vec3(plane);
      const float inv_len = 1.f / glm::length(n);
      return glm::vec4(n * inv_len, plane.w * inv_len);
    };
    const glm::mat4 projection_transpose = glm::transpose(proj);
    const glm::vec4 frustum_x = normalize_plane(projection_transpose[0] + projection_transpose[3]);
    const glm::vec4 frustum_y = normalize_plane(projection_transpose[1] + projection_transpose[3]);
    CullData cd{};
    cd.frustum = glm::vec4(frustum_x.x, frustum_x.z, frustum_y.y, frustum_y.z);
    cd.z_near = 0.1f;
    cd.z_far = 100.f;
    cd.p00 = proj[0][0];
    cd.p11 = proj[1][1];
    cd.pyramid_width = 0;
    cd.pyramid_height = 0;
    cd.pyramid_mip_count = 0;
    cd.paused = 0;
    std::memcpy(ctx_.device->get_buf(cull_storage_buf_.handle)->contents(), &cd, sizeof(cd));

    const size_t task_cmd_bytes = std::max(static_cast<size_t>(tc) * sizeof(TaskCmd), size_t{256});
    RGResourceId task_cmd_dst_rg = ctx_.rg->create_buffer(
        {.size = task_cmd_bytes, .defer_reuse = true}, "meshlet_hello_task_cmds");
    RGResourceId indirect_args_rg = ctx_.rg->create_buffer(
        {.size = std::max(size_t{256}, sizeof(uint32_t) * 3), .defer_reuse = true},
        "meshlet_hello_indirect_args");
    constexpr uint32_t k_visible_obj_cnt_buf_bytes = 256;
    RGResourceId visible_object_count_rg = ctx_.rg->create_buffer(
        {.size = k_visible_obj_cnt_buf_bytes, .defer_reuse = true}, "meshlet_visible_object_count");

    const uint32_t max_draws = ctx_.model_gpu_mgr->instance_mgr().stats().max_instance_data_count;

    {
      auto& p = ctx_.rg->add_transfer_pass("meshlet_clear_visible_obj_count");
      visible_object_count_rg = p.write_buf(visible_object_count_rg, PipelineStage::AllTransfer);
      p.set_ex([this, visible_object_count_rg](CmdEncoder* enc) {
        enc->fill_buffer(ctx_.rg->get_buf(visible_object_count_rg), 0, k_visible_obj_cnt_buf_bytes,
                         0);
      });
    }

    {
      auto& p = ctx_.rg->add_compute_pass("meshlet_clear_indirect");
      indirect_args_rg = p.write_buf(indirect_args_rg, PipelineStage::ComputeShader);
      p.set_ex([this, indirect_args_rg](CmdEncoder* enc) {
        enc->bind_pipeline(clear_indirect_pso_);
        TestClearBufPC pc{};
        pc.buf_idx = ctx_.device->get_buf(ctx_.rg->get_buf(indirect_args_rg))->bindless_idx();
        enc->push_constants(&pc, sizeof(pc));
        enc->dispatch_compute({1, 1, 1}, {1, 1, 1});
      });
    }

    {
      auto& p = ctx_.rg->add_compute_pass("meshlet_prepare_meshlets");
      task_cmd_dst_rg = p.write_buf(task_cmd_dst_rg, PipelineStage::ComputeShader);
      indirect_args_rg = p.rw_buf(indirect_args_rg, PipelineStage::ComputeShader);
      visible_object_count_rg = p.rw_buf(visible_object_count_rg, PipelineStage::ComputeShader);
      p.set_ex([this, task_cmd_dst_rg, indirect_args_rg, visible_object_count_rg,
                max_draws](CmdEncoder* enc) {
        enc->bind_pipeline(prepare_meshlets_pso_);
        auto& geo_batch = ctx_.model_gpu_mgr->geometry_batch();
        DebugMeshletPreparePC pc{};
        pc.dst_task_cmd_buf_idx =
            ctx_.device->get_buf(ctx_.rg->get_buf(task_cmd_dst_rg))->bindless_idx();
        pc.draw_cnt_buf_idx =
            ctx_.device->get_buf(ctx_.rg->get_buf(indirect_args_rg))->bindless_idx();
        pc.instance_data_buf_idx =
            ctx_.device->get_buf(ctx_.model_gpu_mgr->instance_mgr().get_instance_data_buf())
                ->bindless_idx();
        pc.mesh_data_buf_idx =
            ctx_.device->get_buf(geo_batch.mesh_buf.get_buffer_handle())->bindless_idx();
        pc.view_data_buf_idx =
            ctx_.device->get_buf(view_prepare_storage_buf_.handle)->bindless_idx();
        pc.view_data_offset_bytes = 0;
        pc.cull_data_buf_idx = ctx_.device->get_buf(cull_storage_buf_.handle)->bindless_idx();
        pc.cull_data_offset_bytes = 0;
        pc.max_draws = max_draws;
        pc.culling_enabled = gpu_object_frustum_cull_ ? 1u : 0u;
        pc.visible_obj_cnt_buf_idx =
            ctx_.device->get_buf(ctx_.rg->get_buf(visible_object_count_rg))->bindless_idx();
        enc->push_constants(&pc, sizeof(pc));
        enc->dispatch_compute({align_divide_up(static_cast<uint64_t>(max_draws), 64ull), 1, 1},
                              {64, 1, 1});
      });
    }

    {
      auto& p = ctx_.rg->add_graphics_pass("meshlet_hello");
      task_cmd_dst_rg =
          p.read_buf(task_cmd_dst_rg, PipelineStage::MeshShader | PipelineStage::TaskShader);
      indirect_args_rg =
          p.read_buf(indirect_args_rg, PipelineStage::TaskShader | PipelineStage::DrawIndirect,
                     AccessFlags::IndirectCommandRead);
      visible_object_count_rg = p.copy_from_buf(visible_object_count_rg);
      p.w_swapchain_tex(ctx_.swapchain);
      auto depth_att = ctx_.rg->create_texture(
          {
              .format = TextureFormat::D32float,
              .size_class = SizeClass::Swapchain,
          },
          "meshlet_hello_depth_att");

      auto depth_att_id = p.write_depth_output(depth_att);
      p.set_ex([this, task_cmd_dst_rg, indirect_args_rg, visible_object_count_rg, depth_att_id,
                readback_fif_i = ctx_.curr_frame_in_flight_idx](CmdEncoder* enc) {
        flush_pending_model_textures(*ctx_.model_gpu_mgr, *ctx_.device, *ctx_.frame_staging, enc);

        GlobalData gd{};
        gd.render_mode = DEBUG_RENDER_MODE_NONE;
        gd.frame_num = 0;
        gd.meshlet_stats_enabled = 0;
        gd._padding = 0;
        std::memcpy(ctx_.device->get_buf(globals_cb_buf_.handle)->contents(), &gd, sizeof(gd));

        glm::vec4 clear_color{0.06f, 0.07f, 0.09f, 1.f};
        ctx_.device->enqueue_swapchain_for_present(ctx_.swapchain, enc);
        enc->begin_rendering({
            RenderAttInfo::color_att(ctx_.swapchain->get_current_texture(), LoadOp::Clear,
                                     ClearValue{.color = clear_color}),
            RenderAttInfo::depth_stencil_att(
                ctx_.rg->get_att_img(depth_att_id), LoadOp::Clear,
                ClearValue{.depth_stencil = {.depth = 1.f, .stencil = 0}}),
        });
        enc->bind_pipeline(meshlet_pso_);
        enc->set_cull_mode(CullMode::Back);
        enc->set_wind_order(WindOrder::CounterClockwise);
        enc->set_viewport({0, 0}, {ctx_.swapchain->desc_.width, ctx_.swapchain->desc_.height});
        enc->set_scissor({0, 0}, {ctx_.swapchain->desc_.width, ctx_.swapchain->desc_.height});

        auto& geo_batch = ctx_.model_gpu_mgr->geometry_batch();
        enc->bind_srv(geo_batch.mesh_buf.get_buffer_handle(), 5);
        enc->bind_srv(geo_batch.meshlet_buf.get_buffer_handle(), 6);
        enc->bind_srv(geo_batch.meshlet_triangles_buf.get_buffer_handle(), 7);
        enc->bind_srv(geo_batch.meshlet_vertices_buf.get_buffer_handle(), 8);
        enc->bind_srv(geo_batch.vertex_buf.get_buffer_handle(), 9);
        enc->bind_srv(ctx_.model_gpu_mgr->instance_mgr().get_instance_data_buf(), 10);
        enc->bind_srv(ctx_.rg->get_buf(task_cmd_dst_rg), 4);
        enc->bind_srv(ctx_.model_gpu_mgr->materials_allocator().get_buffer_handle(), 11);

        enc->bind_cbv(globals_cb_buf_.handle, GLOBALS_SLOT, 0, sizeof(GlobalData));
        enc->bind_cbv(view_cb_buf_.handle, VIEW_DATA_SLOT, 0, sizeof(ViewData));

        Task2PC task_pc{};
        task_pc.flags = 0;
        task_pc.alpha_test_enabled = 0;
        task_pc.out_draw_count_buf_idx =
            ctx_.device->get_buf(ctx_.rg->get_buf(indirect_args_rg))->bindless_idx();
        enc->push_constants(&task_pc, sizeof(task_pc));

        enc->draw_mesh_threadgroups_indirect(ctx_.rg->get_buf(indirect_args_rg), 0,
                                             {K_TASK_TG_SIZE, 1, 1}, {K_MESH_TG_SIZE, 1, 1});

        if (ctx_.imgui_ui_active && ctx_.imgui_renderer != nullptr) {
          ctx_.imgui_renderer->render(enc,
                                      {ctx_.swapchain->desc_.width, ctx_.swapchain->desc_.height},
                                      ctx_.curr_frame_in_flight_idx);
        }

        enc->end_rendering();

        // Copy indirect groupCountX to CPU readback (outside render pass). Slot matches
        // frames-in-flight fencing (see on_imgui read index).
        auto indirect_h = ctx_.rg->get_buf(indirect_args_rg);
        auto readback_h = draw_count_readback_[readback_fif_i].handle;
        enc->barrier(indirect_h, PipelineStage::DrawIndirect | PipelineStage::TaskShader,
                     AccessFlags::IndirectCommandRead, PipelineStage::AllTransfer,
                     AccessFlags::TransferRead);
        enc->barrier(readback_h, PipelineStage::AllCommands, AccessFlags::AnyWrite,
                     PipelineStage::AllTransfer, AccessFlags::TransferWrite);
        enc->copy_buffer_to_buffer(indirect_h, 0, readback_h, 0, sizeof(uint32_t));
        enc->barrier(readback_h, PipelineStage::AllTransfer, AccessFlags::TransferWrite,
                     PipelineStage::Host, AccessFlags::HostRead);

        auto visible_cnt_h = ctx_.rg->get_buf(visible_object_count_rg);
        auto visible_readback_h = visible_object_count_readback_[readback_fif_i].handle;
        enc->barrier(visible_cnt_h, PipelineStage::ComputeShader, AccessFlags::ShaderStorageWrite,
                     PipelineStage::AllTransfer, AccessFlags::TransferRead);
        enc->barrier(visible_readback_h, PipelineStage::AllCommands, AccessFlags::AnyWrite,
                     PipelineStage::AllTransfer, AccessFlags::TransferWrite);
        enc->copy_buffer_to_buffer(visible_cnt_h, 0, visible_readback_h, 0, sizeof(uint32_t));
        enc->barrier(visible_readback_h, PipelineStage::AllTransfer, AccessFlags::TransferWrite,
                     PipelineStage::Host, AccessFlags::HostRead);
      });
    }
  }

 private:
  void recreate_meshlet_depth_tex() {
    const uint32_t w = ctx_.swapchain->desc_.width;
    const uint32_t h = ctx_.swapchain->desc_.height;
    if (w == 0 || h == 0) {
      // meshlet_depth_tex_ = {};
      return;
    }
    // meshlet_depth_tex_ = ctx_.device->create_tex_h({
    //     .format = TextureFormat::D32float,
    //     .usage = TextureUsage::DepthStencilAttachment,
    //     .dims = {w, h, 1},
    //     .mip_levels = 1,
    //     .array_length = 1,
    //     .name = "meshlet_hello_depth",
    // });
  }

  void recreate_meshlet_pso() {
    auto tex_h = ctx_.swapchain->get_texture(0);
    if (!tex_h.is_valid()) {
      return;
    }
    const auto fmt = ctx_.device->get_tex(tex_h)->desc().format;
    meshlet_pso_ = ctx_.shader_mgr->create_graphics_pipeline({
        .shaders = {{"debug_meshlet_hello", ShaderType::Task},
                    {"debug_meshlet_hello", ShaderType::Mesh},
                    {"debug_meshlet_hello", ShaderType::Fragment}},
        .rendering = {.color_formats = {fmt}, .depth_format = TextureFormat::D32float},
        .depth_stencil = GraphicsPipelineCreateInfo::depth_enable(true, CompareOp::Less),
        .name = "debug_meshlet_hello",
    });
  }

  PipelineHandleHolder meshlet_pso_;
  PipelineHandleHolder clear_indirect_pso_;
  PipelineHandleHolder prepare_meshlets_pso_;
  // TextureHandleHolder meshlet_depth_tex_;
  BufferHandleHolder view_cb_buf_;
  BufferHandleHolder view_prepare_storage_buf_;
  BufferHandleHolder globals_cb_buf_;
  BufferHandleHolder cull_storage_buf_;
  FpsCameraController fps_camera_;
  ModelHandle test_model_handle_;
  InstanceMgr::Alloc instance_alloc_{};
  bool gpu_object_frustum_cull_{true};
  std::array<BufferHandleHolder, k_max_frames_in_flight> draw_count_readback_{};
  std::array<BufferHandleHolder, k_max_frames_in_flight> visible_object_count_readback_{};
  uint32_t meshlet_readback_frames_{0};
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
