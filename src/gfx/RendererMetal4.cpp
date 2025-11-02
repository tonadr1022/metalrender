#include "RendererMetal4.hpp"

#include <Foundation/NSAutoreleasePool.hpp>
#include <QuartzCore/CAMetalLayer.hpp>
#include <span>
#include <tracy/Tracy.hpp>

#include "WindowApple.hpp"
#include "core/EAssert.hpp"
#include "core/Util.hpp"
#include "gfx/CmdEncoder.hpp"
#include "gfx/GFXTypes.hpp"
#include "gfx/ModelLoader.hpp"
#include "gfx/Pipeline.hpp"
#include "gfx/metal/MetalDevice.hpp"
#include "hlsl/material.h"
#include "hlsl/shared_basic_tri.h"

namespace {

using rhi::RenderingAttachmentInfo;
using rhi::ShaderType;
using rhi::TextureFormat;

}  // namespace

void RendererMetal4::init(const CreateInfo& cinfo) {
  device_ = cinfo.device;
  window_ = cinfo.window;
  resource_dir_ = cinfo.resource_dir;

  all_textures_.resize(k_max_textures);

  {
    samplers_.emplace_back(device_->create_sampler_h(rhi::SamplerDesc{
        .min_filter = rhi::FilterMode::Nearest,
        .mag_filter = rhi::FilterMode::Nearest,
        .mipmap_mode = rhi::FilterMode::Nearest,
        .address_mode = rhi::AddressMode::Repeat,
    }));

    samplers_.emplace_back(device_->create_sampler_h(rhi::SamplerDesc{
        .min_filter = rhi::FilterMode::Linear,
        .mag_filter = rhi::FilterMode::Linear,
        .mipmap_mode = rhi::FilterMode::Linear,
        .address_mode = rhi::AddressMode::Repeat,
    }));
  }

  {
    test_pso_ = device_->create_graphics_pipeline_h(rhi::GraphicsPipelineCreateInfo{
        .shaders = {{"basic_tri", ShaderType::Vertex}, {"basic_tri", ShaderType::Fragment}},
        .rendering = {.color_formats{TextureFormat::R8G8B8A8Srgb}},
    });
  }

  material_buf_ = device_->create_buf_h(rhi::BufferDesc{
      .size = k_max_materials * sizeof(M4Material),
      .bindless = true,
      .name = "all materials buf",
  });

  uint32_t albedo_tex = 0;
  M4Material materials[] = {
      M4Material{.color = glm::vec4{1}, .albedo_tex_idx = albedo_tex},
      M4Material{.color = glm::vec4{1}, .albedo_tex_idx = albedo_tex},
      M4Material{.color = glm::vec4{1}, .albedo_tex_idx = albedo_tex},
  };

  device_->copy_to_buffer(materials, sizeof(M4Material) * ARRAY_SIZE(materials),
                          material_buf_.handle, 0);
  ModelInstance out_instance;
  ModelLoadResult out_result;
  ALWAYS_ASSERT(model::load_model(resource_dir_ / "models/Cube/glTF/Cube.gltf", glm::mat4{1},
                                  out_instance, out_result));

  for (auto& upload : out_result.texture_uploads) {
    auto tex = device_->create_tex_h(upload.desc);
    ASSERT(tex.handle.is_valid());
    pending_texture_uploads_.push_back(GPUTexUpload{
        .data = std::move(upload.data),
        .tex = std::move(tex),
        .bytes_per_row = upload.bytes_per_row,
    });
  }

  size_t i = 0;
  for (auto& m : out_result.meshes) {
    auto verts =
        std::span((uint8_t*)out_result.vertices.data() + m.vertex_offset_bytes, m.vertex_count);
    auto vert_copy_size = verts.size() * sizeof(DefaultVertex);
    auto buf = device_->create_buf_h(rhi::BufferDesc{.size = vert_copy_size, .bindless = true});
    device_->copy_to_buffer(verts.data(), vert_copy_size, buf.handle, 0);

    auto index_copy_size = sizeof(uint32_t) * m.index_count;
    auto index_buf =
        device_->create_buf_h(rhi::BufferDesc{.size = index_copy_size, .bindless = true});
    device_->copy_to_buffer((uint8_t*)out_result.indices.data() + m.index_offset, index_copy_size,
                            index_buf.handle, 0);

    meshes_.emplace_back(std::move(buf), std::move(index_buf), i, m.vertex_count, m.index_count);
    i++;
  }

  create_render_target_textures();
}

void RendererMetal4::render([[maybe_unused]] const RenderArgs& args) {
  ZoneScoped;
  curr_frame_idx_ = frame_num_ % device_->get_info().frames_in_flight;

  if (!device_->begin_frame(window_->get_window_size())) {
    return;
  }
  flush_pending_texture_uploads();

  rhi::CmdEncoder* enc = device_->begin_command_list();
  enc->begin_rendering({
      RenderingAttachmentInfo::color_att(device_->get_swapchain().get_texture(curr_frame_idx_),
                                         rhi::LoadOp::Clear, {.color = {0.1, 0.2, 0.1, 1}}),
      RenderingAttachmentInfo::depth_stencil_att(depth_tex_.handle, rhi::LoadOp::Clear,
                                                 {.depth_stencil = {.depth = 1}}),
  });

  enc->bind_pipeline(test_pso_);
  enc->set_depth_stencil_state(rhi::CompareOp::LessOrEqual, true);
  enc->set_wind_order(rhi::WindOrder::CounterClockwise);
  enc->set_cull_mode(rhi::CullMode::Back);
  enc->set_viewport({0, 0}, window_->get_window_size());

  auto win_dims = window_->get_window_size();
  float aspect = (float)win_dims.x / win_dims.y;
  glm::mat4 mv = glm::perspectiveZO(glm::radians(70.f), aspect, 0.01f, 1000.f) * args.view_mat;
  for (auto& mesh : meshes_) {
    {
      BasicTriPC pc{.mvp = mv,
                    .vert_buf_idx = device_->get_buf(mesh.vertex_buf)->bindless_idx(),
                    .mat_buf_idx = device_->get_buf(material_buf_)->bindless_idx(),
                    .mat_buf_id = (uint32_t)mesh.material_id};
      enc->push_constants(&pc, sizeof(pc));
    }
    enc->draw_indexed_primitives(rhi::PrimitiveTopology::TriangleList, mesh.index_buf.handle, 0,
                                 mesh.index_count);
  }

  enc->end_encoding();

  device_->submit_frame();

  frame_num_++;
}

void RendererMetal4::create_render_target_textures() {
  auto dims = window_->get_window_size();
  depth_tex_ = device_->create_tex_h(rhi::TextureDesc{
      .format = rhi::TextureFormat::D32float,
      .storage_mode = rhi::StorageMode::GPUOnly,
      .usage = static_cast<rhi::TextureUsage>(rhi::TextureUsageRenderTarget |
                                              rhi::TextureUsageShaderRead),
      .dims = glm::uvec3{dims, 1},
  });
}

void RendererMetal4::load_model() {}

void RendererMetal4::flush_pending_texture_uploads() {
  if (!pending_texture_uploads_.empty()) {
    rhi::CmdEncoder* enc = device_->begin_command_list();
    for (auto& upload : pending_texture_uploads_) {
      auto* tex = device_->get_tex(upload.tex);
      const auto src_img_size = static_cast<size_t>(upload.bytes_per_row) * tex->desc().dims.y;

      // TODO: scratch buffers
      auto upload_buf_handle = device_->create_buf_h(rhi::BufferDesc{
          .storage_mode = rhi::StorageMode::CPUAndGPU, .size = src_img_size, .bindless = false});

      auto* upload_buf = device_->get_buf(upload_buf_handle);
      memcpy(upload_buf->contents(), upload.data.get(), src_img_size);

      enc->copy_buf_to_tex(upload_buf_handle.handle, 0, upload.bytes_per_row, upload.tex.handle);
      // TODO: mipmaps
      all_textures_[tex->bindless_idx()] = std::move(upload.tex);
    }

    enc->end_encoding();
    pending_texture_uploads_.clear();
  }
}
