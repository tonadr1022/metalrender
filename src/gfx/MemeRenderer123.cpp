#include "MemeRenderer123.hpp"

#include <algorithm>
#include <tracy/Tracy.hpp>

#include "Window.hpp"
#include "core/EAssert.hpp"
#include "core/Logger.hpp"
#include "core/Util.hpp"
#include "default_vertex.h"
#include "gfx/CmdEncoder.hpp"
#include "gfx/GFXTypes.hpp"
#include "gfx/ModelLoader.hpp"
#include "gfx/Pipeline.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/Swapchain.hpp"
#include "hlsl/material.h"
#include "hlsl/shared_basic_indirect.h"
#include "hlsl/shared_basic_tri.h"
#include "hlsl/shared_indirect.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "shader_constants.h"

namespace {

using rhi::RenderingAttachmentInfo;
using rhi::ShaderType;
using rhi::TextureFormat;

}  // namespace

namespace gfx {

void MemeRenderer123::init(const CreateInfo& cinfo) {
  device_ = cinfo.device;
  window_ = cinfo.window;
  resource_dir_ = cinfo.resource_dir;

  {
    // TODO: streamline
    default_white_tex_ =
        device_->create_tex_h(rhi::TextureDesc{.format = rhi::TextureFormat::R8G8B8A8Srgb,
                                               .storage_mode = rhi::StorageMode::GPUOnly,
                                               .dims = glm::uvec3{1, 1, 1},
                                               .mip_levels = 1,
                                               .array_length = 1,
                                               .bindless = true});
    ALWAYS_ASSERT(device_->get_tex(default_white_tex_)->bindless_idx() == 0);
    auto* data = reinterpret_cast<uint64_t*>(malloc(sizeof(uint64_t)));
    *data = 0xFFFFFFFF;
    pending_texture_uploads_.push_back(
        GPUTexUpload{.data = std::unique_ptr<void, UntypedDeleterFuncPtr>(data, &free),
                     .tex = default_white_tex_.handle,
                     .bytes_per_row = 4});
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
        .shaders = {{{"basic_indirect", ShaderType::Vertex},
                     {"basic_indirect", ShaderType::Fragment}}},
        .rendering = {.color_formats{TextureFormat::B8G8R8A8Unorm},
                      .depth_format = TextureFormat::D32float},
    });
  }

  materials_buf_.emplace(*device_,
                         rhi::BufferDesc{.storage_mode = rhi::StorageMode::Default,
                                         .usage = rhi::BufferUsage_Storage,
                                         .size = k_max_materials * sizeof(M4Material),
                                         .bindless = true,
                                         .name = "all materials buf"},
                         sizeof(M4Material));
  instance_data_mgr_.init(128, device_);
  static_draw_batch_.emplace(DrawBatchType::Static, *device_,
                             DrawBatch::CreateInfo{
                                 .initial_vertex_capacity = 1'000'00,
                                 .initial_index_capacity = 1'000'00,
                                 .initial_meshlet_capacity = 100'000,
                                 .initial_mesh_capacity = 20'000,
                                 .initial_meshlet_triangle_capacity = 1'00'000,
                                 .initial_meshlet_vertex_capacity = 1'000'00,
                             });

  scratch_buffer_pool_.emplace(device_);
  imgui_renderer_.emplace(device_);

  rg_.init(device_);
  init_imgui();
}

void MemeRenderer123::render([[maybe_unused]] const RenderArgs& args) {
  ZoneScoped;
  curr_frame_idx_ = frame_num_ % device_->get_info().frames_in_flight;

  if (!device_->begin_frame(window_->get_window_size())) {
    return;
  }

  indirect_cmd_buf_ids_.clear();

  add_render_graph_passes(args);
  static int i = 1;
  rg_.bake(window_->get_window_size(), i++ == 0);
  rg_.execute();

  device_->submit_frame();

  frame_num_++;
}

uint32_t MemeRenderer123::get_bindless_idx(const rhi::BufferHandleHolder& buf) const {
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
    in_use_entries.emplace_back(device_->create_buf_h({
        .storage_mode = rhi::StorageMode::CPUAndGPU,
        .usage = rhi::BufferUsage_Transfer,
        .size = std::max<uint32_t>(1024 * 1024, size),
    }));
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

void MemeRenderer123::add_render_graph_passes(const RenderArgs& args) {
  bool imgui_has_dirty_textures = imgui_renderer_->has_dirty_textures();
  if (!pending_texture_uploads_.empty() || imgui_has_dirty_textures) {
    auto& tex_flush_pass = rg_.add_pass("flush_textures");
    for (const auto& t : pending_texture_uploads_) {
      tex_flush_pass.add_tex(t.tex, (RGAccess)(RGAccess::ComputeWrite | RGAccess::TransferWrite));
    }
    imgui_renderer_->add_dirty_textures_to_pass(tex_flush_pass, false);

    tex_flush_pass.set_execute_fn(
        [this](rhi::CmdEncoder* enc) { flush_pending_texture_uploads(enc); });
  }

  if (indirect_rendering_enabled_) {
    auto& prepare_indirect_pass = rg_.add_pass("prepare_indirect");
    prepare_indirect_pass.add_buf("indirect_buffer", instance_data_mgr_.get_draw_cmd_buf(),
                                  RGAccess::ComputeWrite);
    prepare_indirect_pass.set_execute_fn([this, &args](rhi::CmdEncoder* enc) {
      auto win_dims = window_->get_window_size();
      float aspect = (float)win_dims.x / win_dims.y;
      glm::mat4 mv = glm::perspectiveZO(glm::radians(70.f), aspect, 0.01f, 1000.f) * args.view_mat;
      BasicIndirectPC pc{
          .vp = mv,
          .vert_buf_idx = static_draw_batch_->vertex_buf.get_buffer()->bindless_idx(),
          .instance_data_buf_idx =
              device_->get_buf(instance_data_mgr_.get_instance_data_buf())->bindless_idx(),
          .mat_buf_idx = materials_buf_->get_buffer()->bindless_idx(),
      };

      indirect_cmd_buf_ids_.emplace_back(enc->prepare_indexed_indirect_draws(
          instance_data_mgr_.get_draw_cmd_buf(), 0, all_model_data_.max_objects,
          static_draw_batch_->index_buf.get_buffer_handle(), 0, &pc, sizeof(pc)));
    });
  }

  {
    auto& gbuffer_pass = rg_.add_pass("gbuffer");
    if (indirect_rendering_enabled_) {
      gbuffer_pass.add_buf("indirect_buffer", instance_data_mgr_.get_draw_cmd_buf(),
                           RGAccess::IndirectRead);
    }
    gbuffer_pass.add_tex("gbuffer_a", {.is_swapchain_tex = true}, RGAccess::ColorWrite);
    auto rg_depth_handle = gbuffer_pass.add_tex(
        "depth_tex", {.format = rhi::TextureFormat::D32float}, RGAccess::ColorWrite);
    for (const auto& t : pending_texture_uploads_) {
      gbuffer_pass.add_tex(t.tex, RGAccess::FragmentSample);
    }
    imgui_renderer_->add_dirty_textures_to_pass(gbuffer_pass, true);

    gbuffer_pass.set_execute_fn([this, rg_depth_handle, &args](rhi::CmdEncoder* enc) {
      auto depth_handle = rg_.get_att_img(rg_depth_handle);
      ASSERT(depth_handle.is_valid());
      enc->begin_rendering({
          RenderingAttachmentInfo::color_att(device_->get_swapchain().get_texture(curr_frame_idx_),
                                             rhi::LoadOp::Clear, {.color = {0.1, 0.2, 0.1, 0}}),
          RenderingAttachmentInfo::depth_stencil_att(depth_handle, rhi::LoadOp::Clear,
                                                     {.depth_stencil = {.depth = 1}}),
      });

      enc->bind_pipeline(test2_pso_);
      enc->set_depth_stencil_state(rhi::CompareOp::LessOrEqual, true);
      enc->set_wind_order(rhi::WindOrder::CounterClockwise);
      enc->set_cull_mode(rhi::CullMode::Back);
      enc->set_viewport({0, 0}, window_->get_window_size());

      if (indirect_rendering_enabled_) {
        enc->draw_indexed_indirect(instance_data_mgr_.get_draw_cmd_buf(), indirect_cmd_buf_ids_[0],
                                   0, all_model_data_.max_objects);
      } else {
        // auto win_dims = window_->get_window_size();
        // float aspect = (float)win_dims.x / win_dims.y;
        // glm::mat4 mv =
        //     glm::perspectiveZO(glm::radians(70.f), aspect, 0.01f, 1000.f) * args.view_mat;
        // BasicIndirectPC pc{
        //     .vp = mv,
        //     .vert_buf_idx = static_draw_batch_->vertex_buf.get_buffer()->bindless_idx(),
        //     .instance_data_buf_idx =
        //     get_bindless_idx(instance_data_mgr_.get_instance_data_buf()), .mat_buf_idx =
        //     get_bindless_idx(materials_buf_->get_buffer_handle()),
        // };
        // enc->push_constants(&pc, sizeof(pc));
        // for (size_t i = 0; i < std::min(all_model_data_.max_objects, cmds_.size()); i++) {
        //   auto& cmd = cmds_[i];
        //   enc->draw_indexed_primitives(rhi::PrimitiveTopology::TriangleList,
        //                                static_draw_batch_->index_buf.get_buffer_handle(),
        //                                cmd.first_index, cmd.index_count, 1,
        //                                cmd.vertex_offset / sizeof(DefaultVertex), i);
        // }
      }
      if (args.draw_imgui) {
        imgui_renderer_->render(enc, window_->get_window_size(), curr_frame_idx_);
      }
    });
  }
  {
    auto& pass = rg_.add_pass("shade");
    pass.add_tex("gbuffer_a", {.is_swapchain_tex = true}, RGAccess::ComputeRead);
    pass.set_execute_fn([](rhi::CmdEncoder*) {});
  }
}

void MemeRenderer123::flush_pending_texture_uploads(rhi::CmdEncoder* enc) {
  imgui_renderer_->flush_pending_texture_uploads(enc);

  while (pending_texture_uploads_.size()) {
    auto& upload = pending_texture_uploads_.back();
    auto* tex = device_->get_tex(upload.tex);
    ASSERT(tex);
    ASSERT(upload.data);
    size_t bytes_per_element = 4;
    size_t src_row_bytes = tex->desc().dims.x * bytes_per_element;
    size_t bytes_per_row = align_up(src_row_bytes, 256);
    // TODO: staging buffer pool
    auto upload_buf_handle = device_->create_buf({.size = bytes_per_row * tex->desc().dims.y});
    auto* upload_buf = device_->get_buf(upload_buf_handle);
    size_t dst_offset = 0;
    size_t src_offset = 0;
    for (size_t row = 0; row < tex->desc().dims.y; row++) {
      ASSERT(dst_offset + bytes_per_row <= upload_buf->size());
      memcpy((uint8_t*)upload_buf->contents() + dst_offset,
             (uint8_t*)upload.data.get() + src_offset, src_row_bytes);
      dst_offset += bytes_per_row;
      src_offset += tex->desc().dims.x * bytes_per_element;
    }

    enc->upload_texture_data(upload_buf_handle, 0, bytes_per_row, upload.tex);
    pending_texture_uploads_.pop_back();
  }

  pending_texture_uploads_.clear();
}

bool MemeRenderer123::load_model(const std::filesystem::path& path, const glm::mat4& root_transform,
                                 ModelInstance& model, ModelGPUHandle& out_handle) {
  ModelLoadResult result;
  if (!model::load_model(path, root_transform, model, result)) {
    return false;
  }

  pending_texture_uploads_.reserve(pending_texture_uploads_.size() + result.texture_uploads.size());

  size_t i = 0;
  // TODO: save elsewhere
  std::vector<uint32_t> img_upload_bindless_indices;
  std::vector<rhi::TextureHandleHolder> out_tex_handles;
  img_upload_bindless_indices.resize(result.texture_uploads.size());
  for (auto& upload : result.texture_uploads) {
    if (upload.data) {
      auto tex = device_->create_tex_h(upload.desc);
      img_upload_bindless_indices[i] = device_->get_tex(tex)->bindless_idx();
      pending_texture_uploads_.push_back(GPUTexUpload{.data = std::move(upload.data),
                                                      .tex = tex.handle,
                                                      .bytes_per_row = upload.bytes_per_row});
      out_tex_handles.emplace_back(std::move(tex));
    }
    i++;
  }
  bool resized{};
  assert(!result.materials.empty());
  auto material_alloc = materials_buf_->allocate(result.materials.size(), resized);
  {
    std::vector<M4Material> mats;
    mats.reserve(result.materials.size());
    for (const auto& m : result.materials) {
      mats.push_back(M4Material{.albedo_tex_idx = img_upload_bindless_indices[m.albedo_tex],
                                .color = glm::vec4{m.albedo_factors}});
    }
    device_->copy_to_buffer(mats.data(), mats.size() * sizeof(M4Material),
                            materials_buf_->get_buffer_handle(),
                            material_alloc.offset * sizeof(M4Material));
    if (resized) {
      LINFO("materials resized");
      ASSERT(0);
    }
  }

  if (k_use_mesh_shader) {
    result.indices.clear();
  }
  auto draw_batch_alloc = upload_geometry(DrawBatchType::Static, result.vertices, result.indices,
                                          result.meshlet_process_result, result.meshes);

  std::vector<InstanceData> base_instance_datas;
  std::vector<uint32_t> instance_id_to_node;
  base_instance_datas.reserve(model.tot_mesh_nodes);
  instance_id_to_node.reserve(model.tot_mesh_nodes);

  uint32_t total_instance_vertices{};
  {
    // uint32_t instance_copy_idx = 0;
    // uint32_t curr_meshlet_vis_buf_offset = 0;
    for (size_t node = 0; node < model.nodes.size(); node++) {
      auto mesh_id = model.mesh_ids[node];
      if (model.mesh_ids[node] == Mesh::k_invalid_mesh_id) {
        continue;
      }
      base_instance_datas.emplace_back(InstanceData{
          .mat_id = result.meshes[mesh_id].material_id + material_alloc.offset,
          // .mesh_id = draw_batch_alloc.mesh_alloc.offset + mesh_id,
          // .meshlet_vis_base = curr_meshlet_vis_buf_offset
      });
      instance_id_to_node.push_back(node);
      // instance_copy_idx++;
      // curr_meshlet_vis_buf_offset +=
      //     result.meshlet_process_result.meshlet_datas[mesh_id].meshlets.size();
      total_instance_vertices += result.meshes[mesh_id].vertex_count;
    }
  }

  out_handle = model_gpu_resource_pool_.alloc(ModelGPUResources{
      .material_alloc = material_alloc,
      .static_draw_batch_alloc = draw_batch_alloc,
      .textures = std::move(out_tex_handles),
      .base_instance_datas = std::move(base_instance_datas),
      .meshes = std::move(result.meshes),
      .instance_id_to_node = instance_id_to_node,
      .totals = ModelGPUResources::Totals{.meshlets = static_cast<uint32_t>(
                                              result.meshlet_process_result.tot_meshlet_count),
                                          .vertices = static_cast<uint32_t>(result.vertices.size()),
                                          .instance_vertices = total_instance_vertices},
  });
  return true;
}

DrawBatch::Alloc MemeRenderer123::upload_geometry([[maybe_unused]] DrawBatchType type,
                                                  const std::vector<DefaultVertex>& vertices,
                                                  const std::vector<rhi::DefaultIndexT>& indices,
                                                  const MeshletProcessResult& meshlets,
                                                  std::span<Mesh>) {
  auto& draw_batch = static_draw_batch_;
  ASSERT(!vertices.empty());
  ASSERT(!meshlets.meshlet_datas.empty());

  bool resized{};
  const auto vertex_alloc = draw_batch->vertex_buf.allocate(vertices.size(), resized);
  device_->copy_to_buffer(vertices.data(), vertices.size() * sizeof(DefaultVertex),
                          draw_batch->vertex_buf.get_buffer_handle(),
                          vertex_alloc.offset * sizeof(DefaultVertex));

  OffsetAllocator::Allocation index_alloc{};
  if (!indices.empty()) {
    index_alloc = draw_batch->index_buf.allocate(indices.size(), resized);
    memcpy((reinterpret_cast<rhi::DefaultIndexT*>(draw_batch->index_buf.get_buffer()->contents()) +
            index_alloc.offset),
           indices.data(), indices.size() * sizeof(rhi::DefaultIndexT));
  }

  const auto meshlet_alloc = draw_batch->meshlet_buf.allocate(meshlets.tot_meshlet_count, resized);
  const auto meshlet_vertices_alloc =
      draw_batch->meshlet_vertices_buf.allocate(meshlets.tot_meshlet_verts_count, resized);
  const auto meshlet_triangles_alloc =
      draw_batch->meshlet_triangles_buf.allocate(meshlets.tot_meshlet_tri_count, resized);
  // const auto mesh_alloc = draw_batch->mesh_buf.allocate(meshlets.meshlet_datas.size(), resized);

  size_t meshlet_offset{};
  size_t meshlet_triangles_offset{};
  size_t meshlet_vertices_offset{};
  // size_t mesh_i{};
  for (const auto& meshlet_data : meshlets.meshlet_datas) {
    device_->copy_to_buffer(meshlet_data.meshlets.data(),
                            meshlet_data.meshlets.size() * sizeof(Meshlet),
                            draw_batch->meshlet_buf.get_buffer_handle(),
                            (meshlet_alloc.offset + meshlet_offset) * sizeof(Meshlet));
    meshlet_offset += meshlet_data.meshlets.size();

    device_->copy_to_buffer(
        meshlet_data.meshlet_vertices.data(),
        meshlet_data.meshlet_vertices.size() * sizeof(uint32_t),
        draw_batch->meshlet_vertices_buf.get_buffer_handle(),
        (meshlet_vertices_alloc.offset + meshlet_vertices_offset) * sizeof(uint32_t));

    meshlet_vertices_offset += meshlet_data.meshlet_vertices.size();

    device_->copy_to_buffer(
        meshlet_data.meshlet_triangles.data(),
        meshlet_data.meshlet_triangles.size() * sizeof(uint8_t),
        draw_batch->meshlet_triangles_buf.get_buffer_handle(),
        (meshlet_triangles_alloc.offset + meshlet_triangles_offset) * sizeof(uint8_t));

    meshlet_triangles_offset += meshlet_data.meshlet_triangles.size();

    // MeshData d{
    //     .meshlet_base = meshlet_data.meshlet_base,
    //     .meshlet_count = static_cast<uint32_t>(meshlet_data.meshlets.size()),
    //     .meshlet_vertices_offset = meshlet_data.meshlet_vertices_offset,
    //     .meshlet_triangles_offset = meshlet_data.meshlet_triangles_offset,
    //     .index_count = meshes[mesh_i].index_count,
    //     .index_offset = meshes[mesh_i].index_offset,
    //     .vertex_base = meshes[mesh_i].vertex_offset_bytes,
    //     .vertex_count = meshes[mesh_i].vertex_count,
    //     .center = meshes[mesh_i].center,
    //     .radius = meshes[mesh_i].radius,
    // };
    // memcpy((reinterpret_cast<MeshData*>(draw_batch->mesh_buf.get_buffer()->contents()) +
    // mesh_i +
    //         mesh_alloc.offset),
    //        &d, sizeof(MeshData));
    // mesh_i++;
  }

  return DrawBatch::Alloc{.vertex_alloc = vertex_alloc,
                          .index_alloc = index_alloc,
                          .meshlet_alloc = meshlet_alloc,
                          // .mesh_alloc = mesh_alloc,
                          .meshlet_triangles_alloc = meshlet_triangles_alloc,
                          .meshlet_vertices_alloc = meshlet_vertices_alloc};
}

ModelInstanceGPUHandle MemeRenderer123::add_model_instance(const ModelInstance& model,
                                                           ModelGPUHandle model_gpu_handle) {
  ZoneScoped;
  auto* model_resources = model_gpu_resource_pool_.get(model_gpu_handle);
  ALWAYS_ASSERT(model_resources);
  auto& model_instance_datas = model_resources->base_instance_datas;
  auto& instance_id_to_node = model_resources->instance_id_to_node;
  std::vector<TRS> instance_transforms;
  instance_transforms.reserve(model_instance_datas.size());
  // TODO: no allocs
  std::vector<InstanceData> instance_datas = {model_instance_datas.begin(),
                                              model_instance_datas.end()};
  std::vector<IndexedIndirectDrawCmd> cmds;
  cmds.reserve(instance_datas.size());
  ASSERT(instance_datas.size() == instance_id_to_node.size());

  const OffsetAllocator::Allocation instance_data_gpu_alloc =
      instance_data_mgr_.allocate(model_instance_datas.size());
  all_model_data_.max_objects = instance_data_mgr_.max_seen_size();

  stats_.total_instance_meshlets += model_resources->totals.meshlets;
  stats_.total_instance_vertices += model_resources->totals.instance_vertices;
  OffsetAllocator::Allocation meshlet_vis_buf_alloc{};
  if (k_use_mesh_shader) {
    bool resized{};
    meshlet_vis_buf_alloc = meshlet_vis_buf_->allocate(model_resources->totals.meshlets, resized);
    meshlet_vis_buf_dirty_ = true;
  }

  for (size_t i = 0; i < instance_datas.size(); i++) {
    auto node_i = instance_id_to_node[i];
    instance_transforms.push_back(model.global_transforms[node_i]);
    const auto& transform = model.global_transforms[node_i];
    const auto rot = transform.rotation;
    // TODO: do this initially?
    instance_datas[i].translation = transform.translation;
    instance_datas[i].rotation = glm::vec4{rot[0], rot[1], rot[2], rot[3]};
    instance_datas[i].scale = transform.scale;
    // instance_datas[i].meshlet_vis_base += meshlet_vis_buf_alloc.offset;
    auto& mesh = model_resources->meshes[model.mesh_ids[node_i]];
    cmds.push_back(IndexedIndirectDrawCmd{
        .index_count = mesh.index_count,
        .instance_count = 1,
        .first_index = static_cast<uint32_t>(
            (mesh.index_offset + model_resources->static_draw_batch_alloc.index_alloc.offset *
                                     sizeof(rhi::DefaultIndexT)) /
            sizeof(rhi::DefaultIndexT)),
        .vertex_offset = static_cast<int32_t>(
            mesh.vertex_offset_bytes +
            model_resources->static_draw_batch_alloc.vertex_alloc.offset * sizeof(DefaultVertex)),
        .first_instance = static_cast<uint32_t>(i + instance_data_gpu_alloc.offset),
    });
  }

  device_->copy_to_buffer(instance_datas.data(), instance_datas.size() * sizeof(InstanceData),
                          instance_data_mgr_.get_instance_data_buf(),
                          instance_data_gpu_alloc.offset * sizeof(InstanceData));
  device_->copy_to_buffer(cmds.data(), cmds.size() * sizeof(IndexedIndirectDrawCmd),
                          instance_data_mgr_.get_draw_cmd_buf(),
                          instance_data_gpu_alloc.offset * sizeof(IndexedIndirectDrawCmd));

  return model_instance_gpu_resource_pool_.alloc(
      ModelInstanceGPUResources{.instance_data_gpu_alloc = instance_data_gpu_alloc,
                                .meshlet_vis_buf_alloc = meshlet_vis_buf_alloc});
}

void MemeRenderer123::free_instance(ModelInstanceGPUHandle handle) {
  auto* gpu_resources = model_instance_gpu_resource_pool_.get(handle);
  ASSERT(gpu_resources);
  if (!gpu_resources) {
    return;
  }
  // TODO: anything else?
  instance_data_mgr_.free(gpu_resources->instance_data_gpu_alloc);
  if (meshlet_vis_buf_) {
    meshlet_vis_buf_->free(gpu_resources->meshlet_vis_buf_alloc);
  }
  model_instance_gpu_resource_pool_.destroy(handle);
}

void MemeRenderer123::free_model(ModelGPUHandle handle) {
  auto* gpu_resources = model_gpu_resource_pool_.get(handle);
  ASSERT(gpu_resources);
  if (!gpu_resources) {
    return;
  }
  // TODO: free textures

  materials_buf_->free(gpu_resources->material_alloc);
  static_draw_batch_->free(gpu_resources->static_draw_batch_alloc);
  model_gpu_resource_pool_.destroy(handle);
}

DrawBatch::DrawBatch(DrawBatchType type, rhi::Device& device, const CreateInfo& cinfo)
    : vertex_buf(device,
                 rhi::BufferDesc{.storage_mode = rhi::StorageMode::CPUAndGPU,
                                 .size = cinfo.initial_vertex_capacity * sizeof(DefaultVertex),
                                 .bindless = true},
                 sizeof(DefaultVertex)),
      index_buf(device,
                rhi::BufferDesc{.storage_mode = rhi::StorageMode::CPUAndGPU,
                                .size = cinfo.initial_index_capacity * sizeof(rhi::DefaultIndexT),
                                .bindless = true},
                sizeof(rhi::DefaultIndexT)),
      meshlet_buf(device,
                  rhi::BufferDesc{.storage_mode = rhi::StorageMode::CPUAndGPU,
                                  .size = cinfo.initial_meshlet_capacity * sizeof(Meshlet),
                                  .bindless = true},
                  sizeof(Meshlet)),
      meshlet_triangles_buf(
          device,
          rhi::BufferDesc{.storage_mode = rhi::StorageMode::CPUAndGPU,
                          .size = cinfo.initial_meshlet_triangle_capacity * sizeof(uint8_t),
                          .bindless = true},
          sizeof(uint8_t)),
      meshlet_vertices_buf(
          device,
          rhi::BufferDesc{.storage_mode = rhi::StorageMode::CPUAndGPU,
                          .size = cinfo.initial_meshlet_vertex_capacity * sizeof(uint32_t),
                          .bindless = true},
          sizeof(uint32_t)),
      type(type) {}

DrawBatch::Stats DrawBatch::get_stats() const {
  return {
      .vertex_count = vertex_buf.allocated_element_count(),
      .index_count = index_buf.allocated_element_count(),
      .meshlet_count = meshlet_buf.allocated_element_count(),
      .meshlet_triangle_count = meshlet_triangles_buf.allocated_element_count(),
      .meshlet_vertex_count = meshlet_vertices_buf.allocated_element_count(),
  };
}

OffsetAllocator::Allocation InstanceDataMgr::allocate(size_t element_count) {
  const OffsetAllocator::Allocation alloc = allocator_->allocate(element_count);
  if (alloc.offset == OffsetAllocator::Allocation::NO_SPACE) {
    auto old_capacity = allocator_->capacity();
    auto new_capacity = old_capacity * 2;
    allocator_->grow(allocator_->capacity());
    ASSERT(new_capacity <= allocator_->capacity());
    auto old_instance_data_buf = std::move(instance_data_buf_);
    auto old_draw_cmd_buf = std::move(draw_cmd_buf_);
    allocate_buffers(new_capacity);
    device_->copy_to_buffer(device_->get_buf(old_instance_data_buf)->contents(),
                            old_capacity * sizeof(InstanceData), instance_data_buf_.handle);
    device_->copy_to_buffer(device_->get_buf(old_draw_cmd_buf)->contents(),
                            old_capacity * sizeof(IndexedIndirectDrawCmd), draw_cmd_buf_.handle);
    return allocate(element_count);
  }
  curr_element_count_ += element_count;
  max_seen_size_ = std::max(max_seen_size_, curr_element_count_);
  return alloc;
}

void InstanceDataMgr::free(OffsetAllocator::Allocation alloc) {
  auto element_count = allocator_->allocationSize(alloc);
  // TODO: nooooooooooo memset
  device_->fill_buffer(instance_data_buf_.handle, element_count * sizeof(InstanceData),
                       alloc.offset * sizeof(InstanceData), 0);
  device_->fill_buffer(draw_cmd_buf_.handle, element_count * sizeof(IndexedIndirectDrawCmd),
                       alloc.offset * sizeof(IndexedIndirectDrawCmd), 0);
  allocator_->free(alloc);
  curr_element_count_ -= element_count;
}

void InstanceDataMgr::allocate_buffers(size_t element_count) {
  instance_data_buf_ =
      device_->create_buf_h(rhi::BufferDesc{.storage_mode = rhi::StorageMode::Default,
                                            .usage = rhi::BufferUsage_Storage,
                                            .size = sizeof(InstanceData) * element_count,
                                            .bindless = true});
  draw_cmd_buf_ =
      device_->create_buf_h(rhi::BufferDesc{.storage_mode = rhi::StorageMode::Default,
                                            .usage = rhi::BufferUsage_Indirect,
                                            .size = sizeof(IndexedIndirectDrawCmd) * element_count,
                                            .bindless = true});
}

void MemeRenderer123::init_imgui() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO& io = ImGui::GetIO();
  auto path = (resource_dir_ / "fonts" / "Comic_Sans_MS.ttf");
  io.Fonts->AddFontFromFileTTF(path.c_str(), 16.0f, nullptr, io.Fonts->GetGlyphRangesDefault());

  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.BackendRendererName = "imgui_impl_metal";
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
  // TODO: uuuuuhhhhhhhhhh
  ImGui_ImplGlfw_InitForOther(window_->get_handle(), true);
}

void MemeRenderer123::shutdown_imgui() {
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}

void MemeRenderer123::on_imgui() {
  if (ImGui::TreeNodeEx("Device", ImGuiTreeNodeFlags_DefaultOpen)) {
    device_->on_imgui();
    ImGui::TreePop();
  }
}

}  // namespace gfx
