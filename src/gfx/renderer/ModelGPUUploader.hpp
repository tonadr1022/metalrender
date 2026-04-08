#pragma once

#include "core/Config.hpp"
#include "gfx/DrawBatch.hpp"
#include "gfx/ModelLoader.hpp"
#include "hlsl/shared_instance_data.h"
#include "offsetAllocator.hpp"

namespace TENG_NAMESPACE {

namespace gfx {

struct ModelLoadResult;

struct ModelGPUResources {
  OffsetAllocator::Allocation material_alloc;
  GeometryBatch::Alloc static_draw_batch_alloc;
  std::vector<rhi::TextureHandleHolder> textures;
  std::vector<InstanceData> base_instance_datas;
  std::vector<Mesh> meshes;
  std::vector<uint32_t> instance_id_to_node;
  struct Totals {
    uint32_t meshlets;
    uint32_t vertices;
    uint32_t instance_vertices;
    uint32_t instance_meshlets;
    uint32_t task_cmd_count;
  };
  Totals totals{};
};

struct GPUTexUpload {
  TextureUpload upload;
  rhi::TextureHandle tex;
};

void upload_model(ModelLoadResult& result, ModelInstance& model, rhi::Device& device,
                  std::vector<GPUTexUpload>& pending_texture_uploads,
                  BackedGPUAllocator& materials_buf, BufferCopyMgr& buffer_copy_mgr,
                  GeometryBatch& draw_batch, ModelGPUHandle& out_handle,
                  BlockPool<ModelGPUHandle, ModelGPUResources>& model_gpu_resource_pool);

}  // namespace gfx

}  // namespace TENG_NAMESPACE