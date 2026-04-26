#include "ModelGPUManager.hpp"

#include <tracy/Tracy.hpp>

#include "gfx/renderer/RendererCVars.hpp"
#include "hlsl/material.h"
#include "hlsl/shader_constants.h"

namespace TENG_NAMESPACE {

namespace gfx {

ModelGPUMgr::ModelGPUMgr(rhi::Device& device, InstanceMgr& static_instance_mgr,
                         GeometryBatch& static_draw_batch, BufferCopyMgr& buffer_copy_mgr)
    : device_(&device),
      static_instance_mgr_(static_instance_mgr),
      static_draw_batch_(static_draw_batch),
      buffer_copy_mgr_(buffer_copy_mgr),
      materials_buf_(device, buffer_copy_mgr,
                     gfx::rhi::BufferDesc{
                         .usage = gfx::rhi::BufferUsage::Storage,
                         .size = k_max_materials * sizeof(M4Material),
                         .name = "all materials buf",
                     },
                     sizeof(M4Material)) {}

bool ModelGPUMgr::load_model(const std::filesystem::path& path, const glm::mat4& root_transform,
                             ModelInstance& model, ModelGPUHandle& out_handle) {
  ZoneScoped;
  ModelLoadResult result;
  if (!::teng::gfx::load_model(path, root_transform, model, result)) {
    return false;
  }
  upload_model(result, model, *device_, pending_texture_uploads_, materials_buf_, buffer_copy_mgr_,
               static_draw_batch_, out_handle, model_gpu_resource_pool_);
  return true;
}

void ModelGPUMgr::reserve_space_for(std::span<std::pair<ModelGPUHandle, uint32_t>> models) {
  size_t total_instance_datas{};
  for (auto& [model_handle, instance_count] : models) {
    auto* model_resources = model_gpu_resource_pool_.get(model_handle);
    ASSERT(model_resources);
    total_instance_datas += model_resources->base_instance_datas.size() * instance_count;
  }
  static_instance_mgr_.reserve_space(total_instance_datas);
}

ModelInstanceGPUHandle ModelGPUMgr::add_model_instance(ModelInstance& model,
                                                       ModelGPUHandle model_gpu_handle) {
  ZoneScoped;
  auto* model_resources = model_gpu_resource_pool_.get(model_gpu_handle);
  ASSERT(model_resources);
  auto& model_instance_datas = model_resources->base_instance_datas;
  auto& instance_id_to_node = model_resources->instance_id_to_node;
  std::vector<InstanceData> instance_datas = {model_instance_datas.begin(),
                                              model_instance_datas.end()};
  std::vector<IndexedIndirectDrawCmd> cmds;
  if (renderer_cv::pipeline_mesh_shaders.get() == 0) cmds.reserve(instance_datas.size());

  ASSERT(instance_datas.size() == instance_id_to_node.size());

  const InstanceMgr::Alloc instance_data_gpu_alloc = static_instance_mgr_.allocate(
      model_instance_datas.size(), model_resources->totals.instance_meshlets);
  stats_.total_instance_meshlets += model_resources->totals.instance_meshlets;
  stats_.total_instance_vertices += model_resources->totals.instance_vertices;

  if (static_instance_mgr_.need_draw_cmds_on_cpu() &&
      static_instance_mgr_.cpu_draw_cmds().size() <
          instance_data_gpu_alloc.instance_data_alloc.offset + instance_datas.size()) {
    static_instance_mgr_.cpu_draw_cmds().resize(instance_data_gpu_alloc.instance_data_alloc.offset +
                                                instance_datas.size());
  }
  for (size_t i = 0; i < instance_datas.size(); i++) {
    auto node_i = instance_id_to_node[i];
    const auto& transform = model.global_transforms[node_i];
    instance_datas[i].translation = transform.translation;
    instance_datas[i].rotation = transform.rotation;
    instance_datas[i].scale = transform.scale;
    instance_datas[i].meshlet_vis_base += instance_data_gpu_alloc.meshlet_vis_alloc.offset;
    size_t mesh_id = model.mesh_ids[node_i];
    auto& mesh = model_resources->meshes[mesh_id];
    IndexedIndirectDrawCmd cmd{
        .index_count = mesh.index_count,
        .instance_count = 1,
        .first_index = static_cast<uint32_t>(
            (mesh.index_offset + model_resources->static_draw_batch_alloc.index_alloc.offset *
                                     sizeof(rhi::DefaultIndexT)) /
            sizeof(rhi::DefaultIndexT)),
        .vertex_offset = static_cast<int32_t>(
            mesh.vertex_offset_bytes +
            model_resources->static_draw_batch_alloc.vertex_alloc.offset * sizeof(DefaultVertex)),
        .first_instance =
            static_cast<uint32_t>(i + instance_data_gpu_alloc.instance_data_alloc.offset),
    };
    if (static_instance_mgr_.need_draw_cmds_on_cpu()) {
      auto final_instance_i = instance_data_gpu_alloc.instance_data_alloc.offset + i;
      ASSERT(final_instance_i < static_instance_mgr_.cpu_draw_cmds().size());
      static_instance_mgr_.cpu_draw_cmds()[final_instance_i] = cmd;
    }
    if (renderer_cv::pipeline_mesh_shaders.get() == 0) {
      cmds.push_back(cmd);
    }
  }
  static_draw_batch_.task_cmd_count += model_resources->totals.task_cmd_count;

  stats_.total_instances += instance_datas.size();
  buffer_copy_mgr_.copy_to_buffer(
      instance_datas.data(), instance_datas.size() * sizeof(InstanceData),
      static_instance_mgr_.get_instance_data_buf(),
      instance_data_gpu_alloc.instance_data_alloc.offset * sizeof(InstanceData),
      rhi::PipelineStage::AllCommands, rhi::AccessFlags::ShaderRead);
  if (renderer_cv::pipeline_mesh_shaders.get() == 0) {
    buffer_copy_mgr_.copy_to_buffer(
        cmds.data(), cmds.size() * sizeof(IndexedIndirectDrawCmd),
        static_instance_mgr_.get_draw_cmd_buf(),
        instance_data_gpu_alloc.instance_data_alloc.offset * sizeof(IndexedIndirectDrawCmd),
        rhi::PipelineStage::ComputeShader | rhi::PipelineStage::DrawIndirect,
        rhi::AccessFlags::IndirectCommandRead);
  }
  ASSERT(model_gpu_handle.is_valid());
  return model_instance_gpu_resource_pool_.alloc(ModelInstanceGPUResources{
      .instance_data_gpu_alloc = instance_data_gpu_alloc,
      .model_resources_handle = model_gpu_handle,
  });
}

void ModelGPUMgr::free_instance(ModelInstanceGPUHandle handle) {
  auto* gpu_resources = model_instance_gpu_resource_pool_.get(handle);
  ASSERT(gpu_resources);
  if (!gpu_resources) {
    return;
  }
  static_instance_mgr_.free(gpu_resources->instance_data_gpu_alloc, curr_frame_idx_);
  auto* model_resources = model_gpu_resource_pool_.get(gpu_resources->model_resources_handle);
  static_draw_batch_.task_cmd_count -= model_resources->totals.task_cmd_count;
  stats_.total_instances -= model_resources->base_instance_datas.size();
  stats_.total_instance_meshlets -= model_resources->totals.instance_meshlets;
  stats_.total_instance_vertices -= model_resources->totals.instance_vertices;
  model_instance_gpu_resource_pool_.destroy(handle);
}

void ModelGPUMgr::free_model(ModelGPUHandle handle) {
  auto* gpu_resources = model_gpu_resource_pool_.get(handle);
  ASSERT(gpu_resources);
  if (!gpu_resources) {
    return;
  }
  gpu_resources->textures.clear();
  materials_buf_.free(gpu_resources->material_alloc);
  static_draw_batch_.free(gpu_resources->static_draw_batch_alloc);
  model_gpu_resource_pool_.destroy(handle);
}

}  // namespace gfx

}  // namespace TENG_NAMESPACE