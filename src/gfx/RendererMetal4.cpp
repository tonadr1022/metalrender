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

// glm::mat4 calc_matrix(const TRS& trs) {
//   return glm::translate(glm::mat4{1}, trs.translation) * glm::mat4_cast(trs.rotation) *
//          glm::scale(glm::mat4{1}, glm::vec3{trs.scale});
// }

}  // namespace

namespace gfx {

void RendererMetal4::init(const CreateInfo& cinfo) {
  device_ = cinfo.device;
  window_ = cinfo.window;
  resource_dir_ = cinfo.resource_dir;

  all_textures_.resize(k_max_textures);

  {
    // TODO: streamline
    default_white_tex_ =
        device_->create_tex_h(rhi::TextureDesc{.format = rhi::TextureFormat::R8G8B8A8Unorm,
                                               .storage_mode = rhi::StorageMode::GPUOnly,
                                               .dims = glm::uvec3{1, 1, 1},
                                               .mip_levels = 1,
                                               .array_length = 1,
                                               .bindless = true});
    ALWAYS_ASSERT(device_->get_tex(default_white_tex_)->bindless_idx() == 0);
    auto* data = reinterpret_cast<uint64_t*>(malloc(sizeof(uint64_t)));
    *data = 0xFFFFFFFF;
    pending_texture_uploads_.push_back(
        GPUTexUpload{.data = data, .tex = std::move(default_white_tex_), .bytes_per_row = 4});
  }
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
  // ALWAYS_ASSERT(model::load_model(resource_dir_ / "models/Cube/glTF/Cube.gltf", glm::mat4{1},
  //                                 out_instance, out_result));
  ALWAYS_ASSERT(model::load_model(resource_dir_ / "models/glTF/Models/Sponza/glTF/Sponza.gltf",
                                  glm::mat4{1}, out_instance, out_result));

  std::vector<uint32_t> img_upload_bindless_indices;
  img_upload_bindless_indices.resize(out_result.texture_uploads.size());
  {
    size_t i = 0;
    for (auto& upload : out_result.texture_uploads) {
      if (upload.data) {
        auto tex = device_->create_tex_h(upload.desc);
        ASSERT(tex.handle.is_valid());
        img_upload_bindless_indices[i] = device_->get_tex(tex)->bindless_idx();
        pending_texture_uploads_.push_back(GPUTexUpload{
            .data = upload.data,
            .tex = std::move(tex),
            .bytes_per_row = upload.bytes_per_row,
        });
      }
      i++;
    }
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
      // if (draw_cmd_idx > 0) break;
      const auto& mesh = out_result.meshes[mesh_id];
      cmds.emplace_back(IndexedIndirectDrawCmd{
          .index_count = mesh.index_count,
          .instance_count = 1,
          .first_index = static_cast<uint32_t>(mesh.index_offset / sizeof(rhi::DefaultIndexT)),
          .vertex_offset = static_cast<int32_t>(mesh.vertex_offset_bytes),
          .first_instance = draw_cmd_idx,
      });
      const auto& trs = out_instance.global_transforms[node];
      instance_datas.emplace_back(InstData{
          .translation = trs.translation,
          .rotation = glm::vec4{trs.rotation.x, trs.rotation.y, trs.rotation.z, trs.rotation.w},
          .scale = trs.scale,
          .material_id = mesh.material_id,
      });
      // LINFO("material idx {}", mesh.material_id);
      draw_cmd_idx++;
    }

    device_->copy_to_buffer(instance_datas.data(), draw_cmd_count_ * sizeof(InstData),
                            instance_data_buf_.handle, 0);
    device_->copy_to_buffer(cmds.data(), draw_cmd_count_ * sizeof(IndexedIndirectDrawCmd),
                            indirect_cmd_buf_.handle, 0);
  }
  {
    std::vector<M4Material> materials;
    materials.reserve(out_result.materials.size());
    // TODO: this texture index is the upload index, which is not going to work with multiple models
    for (auto& m : out_result.materials) {
      // if (i++ > 0) break;
      m.albedo_tex = img_upload_bindless_indices[m.albedo_tex];
      materials.emplace_back(M4Material{.albedo_tex_idx = m.albedo_tex});
    }
    device_->copy_to_buffer(materials.data(), materials.size() * sizeof(M4Material),
                            all_material_buf_.handle, 0);
  }

  // draw_cmd_count_ = ;

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

  rg_.init();
  add_render_graph_passes();
  rg_.bake(true);
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
    while (pending_texture_uploads_.size()) {
      auto& upload = pending_texture_uploads_.back();
      auto* tex = device_->get_tex(upload.tex);
      ASSERT(tex);
      ASSERT(upload.data);
      size_t bytes_per_element = 4;
      size_t bytes_per_row = align_up(tex->desc().dims.x * bytes_per_element, 256);
      auto upload_buf_handle = device_->create_buf({.size = bytes_per_row * tex->desc().dims.y});
      auto* upload_buf = device_->get_buf(upload_buf_handle);
      size_t dst_offset = 0;
      size_t src_offset = 0;
      for (size_t row = 0; row < tex->desc().dims.y; row++) {
        memcpy((uint8_t*)upload_buf->contents() + dst_offset, (uint8_t*)upload.data + src_offset,
               bytes_per_row);
        dst_offset += bytes_per_row;
        src_offset += tex->desc().dims.x * bytes_per_element;
      }

      enc->upload_texture_data(upload_buf_handle, 0, bytes_per_row, upload.tex.handle);
      // TODO: mipmaps
      all_textures_[tex->bindless_idx()] = std::move(upload.tex);
      pending_texture_uploads_.pop_back();
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

  size_t best_size = SIZE_T_MAX;
  int best_idx = -1;
  for (size_t i = 0; i < entries.size(); i++) {
    auto* buf = device_->get_buf(entries[i]);
    ASSERT(buf);
    if (buf->size() < best_size && buf->size() >= size) {
      best_idx = i;
      best_size = buf->size();
    }
  }

  if (best_idx == -1) {
    // create new buf
    in_use_entries.emplace_back(
        device_->create_buf_h({.storage_mode = rhi::StorageMode::CPUOnly,
                               .size = std::max<uint32_t>(1024 * 1024, size)}));
  } else {
    auto e = std::move(entries[best_idx]);
    entries.erase(entries.begin() + best_idx);
    in_use_entries.push_back(std::move(e));
  }
  ASSERT(!in_use_entries.empty());
  return in_use_entries.back().handle;
}

void ScratchBufferPool::reset(size_t frame_idx) {
  frame_idx_ = frame_idx;
  auto& entries = frames_[frame_idx_].entries;
  auto& in_use_entries = frames_[frame_idx_].in_use_entries;
  entries.reserve(entries.size() + in_use_entries.size());
  for (auto& e : in_use_entries) {
    entries.emplace_back(std::move(e));
  }
  in_use_entries.clear();
}

void RendererMetal4::add_render_graph_passes() {
  {
    auto& pass = rg_.add_pass("gbuffer_pass");
    pass.add("gbuffer_a", {}, RGAccess::ColorWrite);
    pass.set_execute_fn([]() {});
  }
  {
    auto& pass = rg_.add_pass("shade");
    pass.add("gbuffer_a", {}, RGAccess::ComputeRead);
    pass.set_execute_fn([]() {});
  }
}

}  // namespace gfx
