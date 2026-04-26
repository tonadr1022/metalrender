#include "ResourceManager.hpp"

#include <array>
#include <tracy/Tracy.hpp>

#include "core/Config.hpp"
#include "core/EAssert.hpp"
#include "gfx/ModelGPUManager.hpp"

namespace TENG_NAMESPACE {

ModelHandle ResourceManager::load_model(const std::filesystem::path &path,
                                        const glm::mat4 &root_transform) {
  ZoneScoped;
  auto path_hash = std::hash<std::string>{}(path.string());
  ModelCacheEntry *model = get_model_from_cache(path);
  reserve_model_instances(std::array<ModelInstanceReserveRequest, 1>{
      ModelInstanceReserveRequest{.path = path, .instance_count = 1},
  });
  model->use_count++;
  // copy the model data (transforms, node hierarchy, etc.) into a new instance
  ModelInstance new_instance = model->model;
  new_instance.set_transform(0, root_transform);
  new_instance.update_transforms();
  const auto model_instance_gpu_handle =
      model_gpu_mgr_->add_model_instance(new_instance, model->gpu_resource_handle);
  new_instance.instance_gpu_handle = model_instance_gpu_handle;
  new_instance.model_gpu_handle = model->gpu_resource_handle;
  ModelHandle handle = model_instance_pool_.alloc(std::move(new_instance), path_hash);
  tot_instances_loaded_++;

  return handle;
}

void ResourceManager::free_model(ModelHandle handle) {
  auto *model = model_instance_pool_.get(handle);
  ASSERT(model);
  auto it = model_cache_.find(model->model_path_hash);
  ASSERT(it != model_cache_.end());
  auto &entry = it->second;
  entry.use_count--;
  model_gpu_mgr_->free_instance(model->instance.instance_gpu_handle);
  tot_instances_loaded_--;

  if (entry.use_count == 0 && should_release_unused_models_) {
    model_gpu_mgr_->free_model(entry.gpu_resource_handle);
    model_cache_.erase(it);
  }
}

void ResourceManager::update() {}

ResourceManager::ResourceManager(const CreateInfo &cinfo) : model_gpu_mgr_(cinfo.model_gpu_mgr) {}

void ResourceManager::reserve_model_instances(
    const std::span<const ModelInstanceReserveRequest> &models_to_reserve) {
  std::vector<std::pair<ModelGPUHandle, uint32_t>> requests;
  requests.reserve(models_to_reserve.size());
  for (const ModelInstanceReserveRequest &req : models_to_reserve) {
    if (req.instance_count == 0) {
      continue;
    }
    ModelCacheEntry *model = get_model_from_cache(req.path);
    if (!model) {
      continue;
    }
    requests.emplace_back(model->gpu_resource_handle, req.instance_count);
  }
  if (!requests.empty()) {
    model_gpu_mgr_->reserve_space_for(requests);
  }
}

std::vector<std::vector<ModelHandle>> ResourceManager::load_instanced_models(
    const std::span<const InstancedModelLoadRequest> &models_to_load) {
  std::vector<std::vector<ModelHandle>> result;
  result.reserve(models_to_load.size());

  std::vector<ModelInstanceReserveRequest> reserve_requests;
  reserve_requests.reserve(models_to_load.size());
  for (const InstancedModelLoadRequest &instance_load_req : models_to_load) {
    reserve_requests.push_back(ModelInstanceReserveRequest{
        .path = instance_load_req.path,
        .instance_count = static_cast<uint32_t>(instance_load_req.instance_transforms.size()),
    });
  }
  reserve_model_instances(reserve_requests);

  for (const InstancedModelLoadRequest &instance_load_req : models_to_load) {
    auto &out_handles = result.emplace_back();
    ModelCacheEntry *model = get_model_from_cache(instance_load_req.path);
    model->use_count += instance_load_req.instance_transforms.size();
    out_handles.reserve(instance_load_req.instance_transforms.size());
    for (const auto &inst_transform : instance_load_req.instance_transforms) {
      ModelInstance new_instance = model->model;
      new_instance.set_transform(0, inst_transform);
      new_instance.update_transforms();
      const auto model_instance_gpu_handle =
          model_gpu_mgr_->add_model_instance(new_instance, model->gpu_resource_handle);
      new_instance.instance_gpu_handle = model_instance_gpu_handle;
      new_instance.model_gpu_handle = model->gpu_resource_handle;
      const ModelHandle handle =
          model_instance_pool_.alloc(std::move(new_instance), model->path_hash);
      tot_instances_loaded_++;
      out_handles.emplace_back(handle);
    }
  }

  return result;
}

ResourceManager::ModelCacheEntry *ResourceManager::get_model_from_cache(
    const std::filesystem::path &path) {
  auto path_hash = std::hash<std::string>{}(path.string());
  auto model_resources_it = model_cache_.find(path_hash);
  ModelCacheEntry *cache_entry{};
  if (model_resources_it == model_cache_.end()) {
    ModelInstance model{};
    ModelGPUHandle gpu_handle{};
    if (!model_gpu_mgr_->load_model(path.string(), glm::mat4{1}, model, gpu_handle)) {
      return {};
    }
    tot_models_loaded_++;
    auto result = model_cache_.emplace(path_hash, ModelCacheEntry{.model = std::move(model),
                                                                  .gpu_resource_handle = gpu_handle,
                                                                  .use_count = 0,
                                                                  .path_hash = path_hash});
    cache_entry = &result.first->second;
  } else {
    cache_entry = &model_resources_it->second;
  }
  return cache_entry;
}

}  // namespace TENG_NAMESPACE
