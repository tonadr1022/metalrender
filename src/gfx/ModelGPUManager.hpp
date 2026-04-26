#pragma once

#include <optional>
#include <span>

#include "core/Config.hpp"
#include "core/Pool.hpp"
#include "gfx/BackedGPUAllocator.hpp"
#include "gfx/RendererTypes.hpp"
#include "gfx/renderer/InstanceMgr.hpp"
#include "gfx/renderer/ModelGPUUploader.hpp"

namespace TENG_NAMESPACE {

namespace gfx {

namespace rhi {

class Device;

}

struct ModelInstanceGPUResources {
  InstanceMgr::Alloc instance_data_gpu_alloc;
  ModelGPUHandle model_resources_handle;
};

class ModelGPUMgr {
 public:
  explicit ModelGPUMgr(rhi::Device& device, InstanceMgr& static_instance_mgr,
                       GeometryBatch& static_draw_batch, BufferCopyMgr& buffer_copy_mgr);
  ~ModelGPUMgr() = default;
  ModelGPUMgr(const ModelGPUMgr&) = delete;
  ModelGPUMgr(ModelGPUMgr&&) = delete;
  ModelGPUMgr& operator=(const ModelGPUMgr&) = delete;
  ModelGPUMgr& operator=(ModelGPUMgr&&) = delete;

  bool load_model(const std::filesystem::path& path, const glm::mat4& root_transform,
                  ModelInstance& model, ModelGPUHandle& out_handle);
  void set_curr_frame_idx(uint32_t curr_frame_idx) { curr_frame_idx_ = curr_frame_idx; }
  void reserve_space_for(std::span<std::pair<ModelGPUHandle, uint32_t>> models);
  ModelInstanceGPUHandle add_model_instance(ModelInstance& model, ModelGPUHandle model_gpu_handle);
  void free_instance(ModelInstanceGPUHandle handle);
  void free_model(ModelGPUHandle handle);
  struct Stats {
    uint32_t total_instance_meshlets;
    uint32_t total_instance_vertices;
    uint32_t total_instances;
  };
  const Stats& get_stats() const { return stats_; }

  const std::vector<GPUTexUpload>& get_pending_texture_uploads() const {
    return pending_texture_uploads_;
  }
  void clear_pending_texture_uploads() { pending_texture_uploads_.clear(); }

  GeometryBatch& geometry_batch() { return static_draw_batch_; }
  const GeometryBatch& geometry_batch() const { return static_draw_batch_; }
  InstanceMgr& instance_mgr() { return static_instance_mgr_; }
  const InstanceMgr& instance_mgr() const { return static_instance_mgr_; }
  BackedGPUAllocator& materials_allocator() { return materials_buf_; }
  const BackedGPUAllocator& materials_allocator() const { return materials_buf_; }

  [[nodiscard]] const ModelGPUResources* model_resources(ModelGPUHandle h) const {
    return model_gpu_resource_pool_.get(h);
  }

  [[nodiscard]] std::optional<InstanceMgr::Alloc> instance_alloc(ModelInstanceGPUHandle h) const {
    const auto* p = model_instance_gpu_resource_pool_.get(h);
    if (!p) {
      return std::nullopt;
    }
    return p->instance_data_gpu_alloc;
  }

 private:
  Stats stats_{};
  rhi::Device* device_{};
  InstanceMgr& static_instance_mgr_;
  GeometryBatch& static_draw_batch_;
  BufferCopyMgr& buffer_copy_mgr_;
  BackedGPUAllocator materials_buf_;
  std::vector<GPUTexUpload> pending_texture_uploads_;
  uint32_t curr_frame_idx_{UINT32_MAX};
  BlockPool<ModelGPUHandle, ModelGPUResources> model_gpu_resource_pool_{20, 1, true};
  BlockPool<ModelInstanceGPUHandle, ModelInstanceGPUResources> model_instance_gpu_resource_pool_{
      1024, 5, true};
};

}  // namespace gfx

}  // namespace TENG_NAMESPACE