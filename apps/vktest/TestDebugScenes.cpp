#include "TestDebugScenes.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

#include "Camera.hpp"
#include "ResourceManager.hpp"
#include "core/EAssert.hpp"
#include "core/Util.hpp"
#include "gfx/GPUFrameAllocator2.hpp"
#include "gfx/ImGuiRenderer.hpp"
#include "gfx/ModelGPUManager.hpp"
#include "gfx/ModelLoader.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/ShaderManager.hpp"
#include "gfx/renderer/InstanceMgr.hpp"
#include "gfx/renderer/ModelGPUUploader.hpp"
#include "gfx/rhi/Buffer.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/Pipeline.hpp"
#include "gfx/rhi/Swapchain.hpp"
#include "gfx/rhi/Texture.hpp"
#include "hlsl/default_vertex.h"
#include "hlsl/shader_constants.h"
#include "hlsl/shared_globals.h"
#include "hlsl/shared_task_cmd.h"
#include "ktx.h"

using namespace teng;
using namespace teng::gfx;
using namespace teng::gfx::rhi;

namespace teng::gfx {

namespace {

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
                                  ctx_.curr_frame_idx);

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

    const char* suzanne_path = "Models/Suzanne/glTF/Suzanne.gltf";
    test_model_handle_ = ResourceManager::get().load_model(
        resolve_model_path(ctx_.resource_dir, suzanne_path), glm::mat4{1.f});
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
    globals_cb_buf_ = ctx_.device->create_buf_h({
        .usage = BufferUsage::Uniform,
        .size = align_up(sizeof(GlobalData), k_ubo_align),
        .flags = BufferDescFlags::CPUAccessible,
        .name = "meshlet_hello_globals",
    });

    camera_.pos = {0.f, 0.f, 3.f};
    camera_.pitch = 0.f;
    camera_.yaw = -90.f;
    camera_.calc_vectors();

    recreate_meshlet_pso();
  }

  void shutdown() override { ResourceManager::get().free_model(test_model_handle_); }

  void on_swapchain_resize() override { recreate_meshlet_pso(); }

  void add_render_graph_passes() override {
    auto& batch = ctx_.model_gpu_mgr->geometry_batch();
    const uint32_t tc = batch.task_cmd_count;
    if (tc == 0 || batch.get_stats().vertex_count == 0) {
      return;
    }

    RGResourceId task_cmd_rg = ctx_.rg->create_buffer(
        {.size = std::max(static_cast<size_t>(tc) * sizeof(TaskCmd), size_t{256}),
         .defer_reuse = true},
        "meshlet_hello_task_cmds");

    auto& upload_pass = ctx_.rg->add_transfer_pass("meshlet_hello_task_cmd_upload");
    upload_pass.write_buf(task_cmd_rg, PipelineStage::AllTransfer);
    upload_pass.set_ex([this, task_cmd_rg, tc](CmdEncoder* enc) {
      std::vector<TaskCmd> cmds;
      ModelInstance* inst = ResourceManager::get().get_model(test_model_handle_);
      ASSERT(inst);
      const ModelGPUResources* res = ctx_.model_gpu_mgr->model_resources(inst->model_gpu_handle);
      ASSERT(res);
      append_meshlet_task_cmds(*res, instance_alloc_.instance_data_alloc.offset, cmds);
      ASSERT(static_cast<uint32_t>(cmds.size()) == tc);
      auto bytes = static_cast<uint32_t>(cmds.size() * sizeof(TaskCmd));
      auto upload = ctx_.frame_staging->alloc(bytes);
      std::memcpy(upload.write_ptr, cmds.data(), bytes);
      enc->copy_buffer_to_buffer(upload.buf, upload.offset, ctx_.rg->get_buf(task_cmd_rg), 0,
                                 bytes);
    });

    auto& p = ctx_.rg->add_graphics_pass("meshlet_hello");
    p.read_buf(task_cmd_rg, PipelineStage::MeshShader | PipelineStage::TaskShader);
    p.w_swapchain_tex(ctx_.swapchain);
    p.set_ex([this, task_cmd_rg, tc](CmdEncoder* enc) {
      flush_pending_model_textures(*ctx_.model_gpu_mgr, *ctx_.device, *ctx_.frame_staging, enc);

      const float aspect = static_cast<float>(ctx_.swapchain->desc_.width) /
                           std::max(1.f, static_cast<float>(ctx_.swapchain->desc_.height));
      glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(60.f), aspect, 0.1f, 100.f);
      proj[1][1] = -proj[1][1];
      camera_.calc_vectors();
      glm::mat4 view = camera_.get_view_mat();
      view = glm::lookAt(glm::vec3(5, 5, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
      const float t = ctx_.time_sec;
      const glm::mat4 model = glm::rotate(glm::mat4(1.f), t * 0.5f, glm::vec3(0.f, 1.f, 0.f));
      const glm::mat4 vp = proj * view * model;

      ViewData vd{};
      vd.vp = vp;
      vd.inv_vp = glm::inverse(vp);
      vd.view = view * model;
      vd.proj = proj;
      vd.inv_proj = glm::inverse(proj);
      vd.camera_pos = glm::vec4(camera_.pos, 1.f);
      std::memcpy(ctx_.device->get_buf(view_cb_buf_.handle)->contents(), &vd, sizeof(vd));

      GlobalData gd{};
      gd.render_mode = DEBUG_RENDER_MODE_NONE;
      gd.frame_num = 0;
      gd.meshlet_stats_enabled = 0;
      gd._padding = 0;
      std::memcpy(ctx_.device->get_buf(globals_cb_buf_.handle)->contents(), &gd, sizeof(gd));

      glm::vec4 clear_color{0.06f, 0.07f, 0.09f, 1.f};
      ctx_.device->begin_swapchain_rendering(ctx_.swapchain, enc, &clear_color);
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
      enc->bind_srv(ctx_.rg->get_buf(task_cmd_rg), 4);
      enc->bind_srv(ctx_.model_gpu_mgr->materials_allocator().get_buffer_handle(), 11);

      enc->bind_cbv(globals_cb_buf_.handle, GLOBALS_SLOT, 0, sizeof(GlobalData));
      enc->bind_cbv(view_cb_buf_.handle, VIEW_DATA_SLOT, 0, sizeof(ViewData));

      enc->draw_mesh_threadgroups({tc, 1, 1}, {K_TASK_TG_SIZE, 1, 1}, {K_MESH_TG_SIZE, 1, 1});
      enc->end_rendering();
    });
  }

 private:
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
        .rendering = {.color_formats = {fmt}},
        .depth_stencil = GraphicsPipelineCreateInfo::depth_disable(),
        .name = "debug_meshlet_hello",
    });
  }

  PipelineHandleHolder meshlet_pso_;
  BufferHandleHolder view_cb_buf_;
  BufferHandleHolder globals_cb_buf_;
  Camera camera_;
  ModelHandle test_model_handle_;
  InstanceMgr::Alloc instance_alloc_{};
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
