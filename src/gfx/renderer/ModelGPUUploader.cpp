#include "ModelGPUUploader.hpp"

#include <tracy/Tracy.hpp>

#include "core/Util.hpp"
#include "gfx/BackedGPUAllocator.hpp"
#include "gfx/DrawBatch.hpp"
#include "gfx/GPUFrameAllocator2.hpp"
#include "gfx/renderer/BufferResize.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/Texture.hpp"
#include "hlsl/material.h"
#include "hlsl/shader_constants.h"
#include "hlsl/shared_instance_data.h"
#include "hlsl/shared_mesh_data.h"
#include "ktx.h"

namespace TENG_NAMESPACE {

namespace gfx {

namespace {

GeometryBatch::Alloc upload_geometry(GeometryBatch& draw_batch, BufferCopyMgr& buffer_copy_mgr,
                                     const std::vector<DefaultVertex>& vertices,
                                     const std::vector<rhi::DefaultIndexT>& indices,
                                     const MeshletProcessResult& meshlets, std::span<Mesh> meshes) {
  ZoneScoped;
  ASSERT(!vertices.empty());
  ASSERT(!meshlets.meshlet_datas.empty());

  bool resized{};
  const auto vertex_alloc = draw_batch.vertex_buf.allocate(vertices.size(), resized);
  buffer_copy_mgr.copy_to_buffer(vertices.data(), vertices.size() * sizeof(DefaultVertex),
                                 draw_batch.vertex_buf.get_buffer_handle(),
                                 vertex_alloc.offset * sizeof(DefaultVertex),
                                 rhi::PipelineStage::VertexShader | rhi::PipelineStage::MeshShader,
                                 rhi::AccessFlags::ShaderRead);

  OffsetAllocator::Allocation index_alloc{};
  if (!indices.empty()) {
    index_alloc = draw_batch.index_buf.allocate(indices.size(), resized);
    buffer_copy_mgr.copy_to_buffer(indices.data(), indices.size() * sizeof(rhi::DefaultIndexT),
                                   draw_batch.index_buf.get_buffer_handle(),
                                   index_alloc.offset * sizeof(rhi::DefaultIndexT),
                                   rhi::PipelineStage::IndexInput, rhi::AccessFlags::IndexRead);
  }

  const auto meshlet_alloc = draw_batch.meshlet_buf.allocate(meshlets.tot_meshlet_count, resized);
  const auto meshlet_vertices_alloc =
      draw_batch.meshlet_vertices_buf.allocate(meshlets.tot_meshlet_verts_count, resized);
  const auto meshlet_triangles_alloc =
      draw_batch.meshlet_triangles_buf.allocate(meshlets.tot_meshlet_tri_count, resized);
  const auto mesh_alloc = draw_batch.mesh_buf.allocate(meshlets.meshlet_datas.size(), resized);

  size_t meshlet_offset{};
  size_t meshlet_triangles_offset{};
  size_t meshlet_vertices_offset{};
  size_t mesh_i{};
  std::vector<MeshData> mesh_datas;
  mesh_datas.reserve(meshlets.meshlet_datas.size());
  for (const auto& meshlet_data : meshlets.meshlet_datas) {
    ASSERT(!meshlet_data.meshlets.empty());
    buffer_copy_mgr.copy_to_buffer(meshlet_data.meshlets.data(),
                                   meshlet_data.meshlets.size() * sizeof(Meshlet),
                                   draw_batch.meshlet_buf.get_buffer_handle(),
                                   (meshlet_alloc.offset + meshlet_offset) * sizeof(Meshlet),
                                   rhi::PipelineStage::MeshShader | rhi::PipelineStage::TaskShader,
                                   rhi::AccessFlags::ShaderRead);
    meshlet_offset += meshlet_data.meshlets.size();

    ASSERT(!meshlet_data.meshlet_vertices.empty());
    buffer_copy_mgr.copy_to_buffer(
        meshlet_data.meshlet_vertices.data(),
        meshlet_data.meshlet_vertices.size() * sizeof(uint32_t),
        draw_batch.meshlet_vertices_buf.get_buffer_handle(),
        (meshlet_vertices_alloc.offset + meshlet_vertices_offset) * sizeof(uint32_t),
        rhi::PipelineStage::MeshShader | rhi::PipelineStage::TaskShader,
        rhi::AccessFlags::ShaderRead);

    meshlet_vertices_offset += meshlet_data.meshlet_vertices.size();

    ASSERT(!meshlet_data.meshlet_triangles.empty());
    buffer_copy_mgr.copy_to_buffer(
        meshlet_data.meshlet_triangles.data(),
        meshlet_data.meshlet_triangles.size() * sizeof(uint8_t),
        draw_batch.meshlet_triangles_buf.get_buffer_handle(),
        (meshlet_triangles_alloc.offset + meshlet_triangles_offset) * sizeof(uint8_t),
        rhi::PipelineStage::MeshShader | rhi::PipelineStage::TaskShader,
        rhi::AccessFlags::ShaderRead);

    meshlet_triangles_offset += meshlet_data.meshlet_triangles.size();

    ASSERT(mesh_i < meshes.size());
    MeshData d{
        .meshlet_base = meshlet_data.meshlet_base + meshlet_alloc.offset,
        .meshlet_count = static_cast<uint32_t>(meshlet_data.meshlets.size()),
        .meshlet_vertices_offset =
            meshlet_data.meshlet_vertices_offset + meshlet_vertices_alloc.offset,
        .meshlet_triangles_offset =
            meshlet_data.meshlet_triangles_offset + meshlet_triangles_alloc.offset,
        .vertex_base = vertex_alloc.offset,
        .center = meshes[mesh_i].center,
        .radius = meshes[mesh_i].radius,
    };
    mesh_i++;
    mesh_datas.push_back(d);
  }
  buffer_copy_mgr.copy_to_buffer(
      mesh_datas.data(), mesh_datas.size() * sizeof(MeshData),
      draw_batch.mesh_buf.get_buffer_handle(), mesh_alloc.offset * sizeof(MeshData),
      rhi::PipelineStage::ComputeShader | rhi::PipelineStage::MeshShader |
          rhi::PipelineStage::TaskShader,
      rhi::AccessFlags::ShaderRead);

  return GeometryBatch::Alloc{.vertex_alloc = vertex_alloc,
                              .index_alloc = index_alloc,
                              .meshlet_alloc = meshlet_alloc,
                              .mesh_alloc = mesh_alloc,
                              .meshlet_triangles_alloc = meshlet_triangles_alloc,
                              .meshlet_vertices_alloc = meshlet_vertices_alloc};
}

}  // namespace

void upload_model(ModelLoadResult& result, ModelInstance& model, rhi::Device& device,
                  std::vector<GPUTexUpload>& pending_texture_uploads,
                  BackedGPUAllocator& materials_buf, BufferCopyMgr& buffer_copy_mgr,
                  GeometryBatch& draw_batch, ModelGPUHandle& out_handle,
                  BlockPool<ModelGPUHandle, ModelGPUResources>& model_gpu_resource_pool) {
  pending_texture_uploads.reserve(pending_texture_uploads.size() + result.texture_uploads.size());

  std::vector<uint32_t> img_upload_bindless_indices(result.texture_uploads.size(), 0);
  std::vector<rhi::TextureHandleHolder> out_tex_handles;
  for (size_t ti = 0; ti < result.texture_uploads.size(); ++ti) {
    auto& upload = result.texture_uploads[ti];
    if (upload.data) {
      auto tex = device.create_tex_h(upload.desc);
      img_upload_bindless_indices[ti] = device.get_tex(tex)->bindless_idx();
      pending_texture_uploads.push_back(
          GPUTexUpload{.upload = std::move(upload), .tex = tex.handle});
      out_tex_handles.emplace_back(std::move(tex));
    }
  }

  bool resized{};
  assert(!result.materials.empty());
  auto material_alloc = materials_buf.allocate(result.materials.size(), resized);

  {
    std::vector<M4Material> mats;
    mats.reserve(result.materials.size());
    for (const auto& m : result.materials) {
      auto& mat = mats.emplace_back();
      if (m.albedo_tex != INVALID_TEX_ID) {
        mat.albedo_tex_idx = img_upload_bindless_indices[m.albedo_tex];
      } else {
        mat.albedo_tex_idx = INVALID_TEX_ID;
      }
      if (m.normal_tex != INVALID_TEX_ID) {
        mat.normal_tex_idx = img_upload_bindless_indices[m.normal_tex];
      } else {
        mat.normal_tex_idx = INVALID_TEX_ID;
      }
      mat.color = m.albedo_factors;
      mat.flags = m.flags;
    }
    buffer_copy_mgr.copy_to_buffer(
        mats.data(), mats.size() * sizeof(M4Material), materials_buf.get_buffer_handle(),
        material_alloc.offset * sizeof(M4Material), rhi::PipelineStage::FragmentShader,
        rhi::AccessFlags::ShaderRead);
    if (resized) {
      ASSERT(0);
    }
  }
  auto draw_batch_alloc =
      upload_geometry(draw_batch, buffer_copy_mgr, result.vertices, result.indices,
                      result.meshlet_process_result, result.meshes);

  std::vector<uint32_t> gpu_meshlet_base;
  gpu_meshlet_base.reserve(result.meshlet_process_result.meshlet_datas.size());
  for (const auto& meshlet_data : result.meshlet_process_result.meshlet_datas) {
    gpu_meshlet_base.push_back(meshlet_data.meshlet_base + draw_batch_alloc.meshlet_alloc.offset);
  }

  std::vector<InstanceData> base_instance_datas;
  std::vector<uint32_t> instance_id_to_node;
  base_instance_datas.reserve(model.tot_mesh_nodes);
  instance_id_to_node.reserve(model.tot_mesh_nodes);

  uint32_t total_instance_vertices{};
  uint32_t total_instance_meshlets{};
  uint32_t task_cmd_count{};
  {
    uint32_t curr_meshlet_vis_buf_i{};
    for (size_t node = 0; node < model.nodes.size(); node++) {
      auto mesh_id = model.mesh_ids[node];
      if (model.mesh_ids[node] == Mesh::k_invalid_mesh_id) {
        continue;
      }
      base_instance_datas.emplace_back(InstanceData{
          .mat_id = result.meshes[mesh_id].material_id + material_alloc.offset,
          .mesh_id = draw_batch_alloc.mesh_alloc.offset + mesh_id,
          .meshlet_vis_base = curr_meshlet_vis_buf_i,
      });
      instance_id_to_node.push_back(node);
      curr_meshlet_vis_buf_i +=
          result.meshlet_process_result.meshlet_datas[mesh_id].meshlets.size();
      total_instance_vertices +=
          result.meshlet_process_result.meshlet_datas[mesh_id].meshlet_vertices.size();
      total_instance_meshlets += result.meshes[mesh_id].meshlet_count;
      task_cmd_count += align_divide_up(result.meshes[mesh_id].meshlet_count, K_TASK_TG_SIZE);
    }
  }

  out_handle = model_gpu_resource_pool.alloc(ModelGPUResources{
      .material_alloc = material_alloc,
      .static_draw_batch_alloc = draw_batch_alloc,
      .textures = std::move(out_tex_handles),
      .base_instance_datas = std::move(base_instance_datas),
      .meshes = std::move(result.meshes),
      .gpu_meshlet_base = std::move(gpu_meshlet_base),
      .instance_id_to_node = instance_id_to_node,
      .totals =
          ModelGPUResources::Totals{
              .meshlets = static_cast<uint32_t>(result.meshlet_process_result.tot_meshlet_count),
              .vertices = static_cast<uint32_t>(result.vertices.size()),
              .instance_vertices = total_instance_vertices,
              .instance_meshlets = total_instance_meshlets,
              .task_cmd_count = task_cmd_count,
          },
  });
}

void upload_texture_data(const GPUTexUpload& upload, rhi::Texture* tex, GPUFrameAllocator3& staging,
                         rhi::CmdEncoder* enc) {
  const auto& tex_upload = upload.upload;
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
      memcpy(static_cast<std::byte*>(upload_buf.write_ptr) + curr_dst_offset,
             reinterpret_cast<const std::byte*>(ktx_tex->pData) + offset, img_mip_level_size_bytes);
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
      memcpy(static_cast<std::byte*>(upload_buf.write_ptr) + dst_offset,
             static_cast<const std::byte*>(tex_upload.data.get()) + src_offset, src_bytes_per_row);
      dst_offset += bytes_per_row;
      src_offset += src_bytes_per_row;
    }
    enc->upload_texture_data(upload_buf.buf, upload_buf.offset, bytes_per_row, upload.tex,
                             glm::uvec3{tex->desc().dims.x, tex->desc().dims.y, 1},
                             glm::uvec3{0, 0, 0}, -1);
  }
}

}  // namespace gfx

}  // namespace TENG_NAMESPACE