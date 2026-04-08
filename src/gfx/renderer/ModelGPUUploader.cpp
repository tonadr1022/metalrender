#include "ModelGPUUploader.hpp"

#include <tracy/Tracy.hpp>

#include "core/Util.hpp"
#include "gfx/BackedGPUAllocator.hpp"
#include "gfx/DrawBatch.hpp"
#include "gfx/renderer/BufferResize.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/Texture.hpp"
#include "hlsl/material.h"
#include "hlsl/shader_constants.h"
#include "hlsl/shared_instance_data.h"
#include "hlsl/shared_mesh_data.h"

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
  size_t i = 0;

  std::vector<uint32_t> img_upload_bindless_indices;
  std::vector<rhi::TextureHandleHolder> out_tex_handles;
  for (auto& upload : result.texture_uploads) {
    if (upload.data) {
      auto tex = device.create_tex_h(upload.desc);
      img_upload_bindless_indices[i] = device.get_tex(tex)->bindless_idx();
      pending_texture_uploads.push_back(
          GPUTexUpload{.upload = std::move(upload), .tex = tex.handle});
      out_tex_handles.emplace_back(std::move(tex));
    }
    i++;
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

  GeometryBatch::Alloc upload_geometry(
      GeometryBatchType type, const std::vector<DefaultVertex>& vertices,
      const std::vector<rhi::DefaultIndexT>& indices, const MeshletProcessResult& meshlets,
      std::span<Mesh> meshes);
  out_handle = model_gpu_resource_pool.alloc(ModelGPUResources{
      .material_alloc = material_alloc,
      .static_draw_batch_alloc = draw_batch_alloc,
      .textures = std::move(out_tex_handles),
      .base_instance_datas = std::move(base_instance_datas),
      .meshes = std::move(result.meshes),
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

}  // namespace gfx

}  // namespace TENG_NAMESPACE