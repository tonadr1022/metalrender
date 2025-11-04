#include "RendererMetal4.hpp"

#include <Foundation/NSAutoreleasePool.hpp>
#include <QuartzCore/CAMetalLayer.hpp>
#include <tracy/Tracy.hpp>

#include "WindowApple.hpp"
#include "core/EAssert.hpp"
#include "gfx/CmdEncoder.hpp"
#include "gfx/GFXTypes.hpp"
#include "gfx/ModelLoader.hpp"
#include "gfx/Pipeline.hpp"
#include "gfx/metal/MetalDevice.hpp"
#include "hlsl/material.h"
#include "hlsl/shared_basic_indirect.h"
#include "hlsl/shared_basic_tri.h"
#include "hlsl/shared_indirect.h"

namespace {

using rhi::RenderingAttachmentInfo;
using rhi::ShaderType;
using rhi::TextureFormat;

glm::mat4 calc_matrix(const TRS& trs) {
  return glm::translate(glm::mat4{1}, trs.translation) * glm::mat4_cast(trs.rotation) *
         glm::scale(glm::mat4{1}, glm::vec3{trs.scale});
}

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
    test2_pso_ = device_->create_graphics_pipeline_h({
        .shaders = {{"basic_indirect", ShaderType::Vertex},
                    {"basic_indirect", ShaderType::Fragment}},
        .rendering = {.color_formats{TextureFormat::R8G8B8A8Srgb}},
    });
  }

  all_material_buf_ = device_->create_buf_h(rhi::BufferDesc{
      .size = k_max_materials * sizeof(M4Material),
      .bindless = true,
      .name = "all materials buf",
  });

  // ALWAYS_ASSERT(model::load_model(resource_dir_ / "models/glTF/Models/Cube/glTF/Cube.gltf",
  //                                 glm::mat4{1}, out_instance, out_result));
  ALWAYS_ASSERT(
      model::load_model(resource_dir_ / "models/glTF/Models/SimpleMeshes/glTF/SimpleMeshes.gltf",
                        glm::mat4{1}, out_instance, out_result));
  // ALWAYS_ASSERT(model::load_model(resource_dir_ / "models/glTF/Models/Sponza/glTF/Sponza.gltf",
  //                                 glm::mat4{1}, out_instance, out_result));

  for (auto& upload : out_result.texture_uploads) {
    auto tex = device_->create_tex_h(upload.desc);
    ASSERT(tex.handle.is_valid());
    pending_texture_uploads_.push_back(GPUTexUpload{
        .data = std::move(upload.data),
        .tex = std::move(tex),
        .bytes_per_row = upload.bytes_per_row,
    });
  }

  constexpr size_t k_max_draws = 1024;
  constexpr size_t k_max_vertices = 2'000'000;
  constexpr size_t k_max_indices = 2'000'000;

  indirect_cmd_buf_ = device_->create_buf_h(
      {.size = sizeof(IndexedIndirectDrawCmd) * k_max_draws, .bindless = true});
  instance_data_buf_ =
      device_->create_buf_h({.size = sizeof(InstData) * k_max_draws, .bindless = true});
  all_static_vertices_buf_ =
      device_->create_buf_h({.size = sizeof(DefaultVertex) * k_max_vertices, .bindless = true});
  all_static_indices_buf_ =
      device_->create_buf_h({.size = sizeof(rhi::DefaultIndexT) * k_max_indices, .bindless = true});

  {
    const auto tot_draws = out_instance.tot_mesh_nodes;
    cmds.reserve(tot_draws);
    draw_cmd_count_ = tot_draws;
    instance_datas.reserve(tot_draws);

    uint32_t draw_cmd_idx = 0;
    for (size_t node = 0; node < out_instance.nodes.size(); node++) {
      auto mesh_id = out_instance.mesh_ids[node];
      if (mesh_id == Mesh::k_invalid_mesh_id) {
        continue;
      }
      const auto& mesh = out_result.meshes[mesh_id];
      cmds.emplace_back(IndexedIndirectDrawCmd{
          .index_count = mesh.index_count,
          .instance_count = 1,
          .first_index = static_cast<uint32_t>(mesh.index_offset / sizeof(rhi::DefaultIndexT)),
          .vertex_offset = static_cast<int32_t>(mesh.vertex_offset_bytes),
          .first_instance = draw_cmd_idx,
      });
      auto model_mat = calc_matrix(out_instance.global_transforms[node]);
      // model_mat = glm::transpose(model_mat);
      LINFO("{} ", model_mat[3][0]);
      instance_datas.emplace_back(InstData{
          .model = model_mat,
          // .material_id = draw_cmd_idx,  // TODO: fixxxxxxxxxx
          // .base_vertex = static_cast<uint32_t>(cmds.back().vertex_offset /
          // sizeof(DefaultVertex)),
      });
      draw_cmd_idx++;
    }

    device_->copy_to_buffer(instance_datas.data(), instance_datas.size() * sizeof(InstData),
                            instance_data_buf_.handle, 0);
    device_->copy_to_buffer(cmds.data(), cmds.size() * sizeof(IndexedIndirectDrawCmd),
                            indirect_cmd_buf_.handle, 0);
  }
  {
    std::vector<M4Material> materials;
    materials.reserve(out_result.materials.size());
    // TODO: this texture index is the upload index, which is not going to work with multiple models
    for (auto& m : out_result.materials) {
      materials.emplace_back(M4Material{.color = m.albedo_factors, .albedo_tex_idx = m.albedo_tex});
    }
    if (materials.empty()) {
      materials.emplace_back(
          M4Material{.color = glm::vec4{1, 0, 0, 1}, .albedo_tex_idx = UINT32_MAX});
      materials.emplace_back(
          M4Material{.color = glm::vec4{0, 0, 1, 1}, .albedo_tex_idx = UINT32_MAX});
    }
    device_->copy_to_buffer(materials.data(), materials.size() * sizeof(M4Material),
                            all_material_buf_.handle, 0);
  }

  {
    size_t vert_copy_size = out_result.vertices.size() * sizeof(DefaultVertex);
    size_t index_copy_size = out_result.indices.size() * sizeof(rhi::DefaultIndexT);
    device_->copy_to_buffer(out_result.vertices.data(), vert_copy_size,
                            all_static_vertices_buf_.handle, 0);
    device_->copy_to_buffer(out_result.indices.data(), index_copy_size,
                            all_static_indices_buf_.handle, 0);
  }

  create_render_target_textures();
  scratch_buffer_pool_.emplace(device_);
}

void RendererMetal4::render([[maybe_unused]] const RenderArgs& args) {
  ZoneScoped;
  curr_frame_idx_ = frame_num_ % device_->get_info().frames_in_flight;

  if (!device_->begin_frame(window_->get_window_size())) {
    return;
  }

  rhi::CmdEncoder* enc = device_->begin_command_list();
  flush_pending_texture_uploads(enc);
  {
    auto win_dims = window_->get_window_size();
    float aspect = (float)win_dims.x / win_dims.y;
    glm::mat4 mv = glm::perspectiveZO(glm::radians(70.f), aspect, 0.01f, 1000.f) * args.view_mat;
    static_assert(sizeof(BasicIndirectPC) == 160);
    BasicIndirectPC pc{
        .vp = mv,
        .vert_buf_idx = get_bindless_idx(all_static_vertices_buf_),
        .instance_data_buf_idx = get_bindless_idx(instance_data_buf_),
        .mat_buf_idx = get_bindless_idx(all_material_buf_),
    };
    enc->push_constants(&pc, sizeof(pc));
    enc->prepare_indexed_indirect_draws(indirect_cmd_buf_.handle, 0, draw_cmd_count_,
                                        all_static_indices_buf_.handle, 0);
  }

  enc->begin_rendering({
      RenderingAttachmentInfo::color_att(device_->get_swapchain().get_texture(curr_frame_idx_),
                                         rhi::LoadOp::Clear, {.color = {0.1, 0.2, 0.1, 1}}),
      RenderingAttachmentInfo::depth_stencil_att(depth_tex_.handle, rhi::LoadOp::Clear,
                                                 {.depth_stencil = {.depth = 1}}),
  });

  enc->bind_pipeline(test2_pso_);
  enc->set_depth_stencil_state(rhi::CompareOp::LessOrEqual, true);
  enc->set_wind_order(rhi::WindOrder::CounterClockwise);
  enc->set_cull_mode(rhi::CullMode::Back);
  enc->set_viewport({0, 0}, window_->get_window_size());

  enc->barrier(rhi::PipelineStage_ComputeShader, rhi::AccessFlags_ShaderWrite,
               rhi::PipelineStage_AllGraphics, rhi::AccessFlags_ShaderRead);

  // for (size_t i = 0; i < std::min(draw_cmd_count_, cmds.size()); i++) {
  // auto& cmd = cmds[i];
  // enc->draw_indexed_primitives(rhi::PrimitiveTopology::TriangleList,
  //                              all_static_indices_buf_.handle, cmd.first_index,
  //                              cmd.index_count, 1, cmd.vertex_offset / sizeof(DefaultVertex),
  //                              i);
  // }
  enc->draw_indexed_indirect(indirect_cmd_buf_.handle, 0, draw_cmd_count_);

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

void RendererMetal4::flush_pending_texture_uploads(rhi::CmdEncoder* enc) {
  if (!pending_texture_uploads_.empty()) {
    // rhi::CmdEncoder* enc = device_->begin_command_list();
    for (auto& upload : pending_texture_uploads_) {
      auto* tex = device_->get_tex(upload.tex);
      ASSERT(tex);
      const auto src_img_size = static_cast<size_t>(upload.bytes_per_row) * tex->desc().dims.y;

      auto upload_buf_handle = get_scratch_buffer_pool().alloc(src_img_size);
      auto* upload_buf = device_->get_buf(upload_buf_handle);
      memcpy(upload_buf->contents(), upload.data.get(), src_img_size);

      enc->copy_buf_to_tex(upload_buf_handle, 0, upload.bytes_per_row, upload.tex.handle);
      // TODO: mipmaps
      all_textures_[tex->bindless_idx()] = std::move(upload.tex);
    }

    pending_texture_uploads_.clear();
  }
}

uint32_t RendererMetal4::get_bindless_idx(const rhi::BufferHandleHolder& buf) const {
  return device_->get_buf(buf)->bindless_idx();
}

rhi::BufferHandle ScratchBufferPool::alloc(size_t size) {
  auto& entries = frames_[frame_idx_].entries;
  auto& in_use_entries = frames_[frame_idx_].in_use_entries;

  size_t min_size = SIZE_T_MAX;
  int min_idx = -1;
  for (size_t i = 0; i < entries.size(); i++) {
    auto* buf = device_->get_buf(entries[i]);
    ASSERT(buf);
    if (buf->size() < min_size) {
      min_idx = i;
      min_size = buf->size();
    }
  }

  if (min_idx == -1) {
    // create new buf
    in_use_entries.emplace_back(
        device_->create_buf_h({.storage_mode = rhi::StorageMode::CPUOnly,
                               .size = std::max<uint32_t>(1024 * 1024, size)}));
  } else {
    auto e = std::move(entries[min_idx]);
    entries.erase(entries.begin() + min_idx);
    in_use_entries.push_back(std::move(e));
  }
  return in_use_entries.back().handle;
}

void ScratchBufferPool::reset(size_t frame_idx) {
  frame_idx_ = frame_idx;
  auto& entries = frames_[frame_idx_].entries;
  auto& in_use_entries = frames_[frame_idx_].in_use_entries;
  entries.reserve(entries.size() + in_use_entries.size());
  entries.insert(entries.end(), std::make_move_iterator(in_use_entries.begin()),
                 std::make_move_iterator(in_use_entries.end()));
  in_use_entries.clear();
}
