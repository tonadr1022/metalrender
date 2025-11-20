#include "ResourceManager.hpp"

#include <tracy/Tracy.hpp>

#include "RendererMetal4.hpp"
#include "core/EAssert.hpp"

ModelHandle ResourceManager::load_model(const std::filesystem::path &path,
                                        const glm::mat4 &root_transform) {
  ZoneScoped;
  auto path_hash = std::hash<std::string>{}(path.string());
  auto model_resources_it = model_cache_.find(path_hash);
  ModelCacheEntry *cache_entry{};
  if (model_resources_it == model_cache_.end()) {
    ModelInstance model{};
    ModelGPUHandle gpu_handle{};
    if (!renderer_->load_model(path.string(), root_transform, model, gpu_handle)) {
      return {};
    }
    auto result = model_cache_.emplace(path_hash, ModelCacheEntry{.model = std::move(model),
                                                                  .gpu_resource_handle = gpu_handle,
                                                                  .use_count = 0});
    cache_entry = &result.first->second;
  } else {
    cache_entry = &model_resources_it->second;
  }

  cache_entry->use_count++;

  auto new_instance = cache_entry->model;
  new_instance.set_transform(0, root_transform);
  new_instance.update_transforms();

  const auto model_instance_gpu_handle =
      renderer_->add_model_instance(new_instance, cache_entry->gpu_resource_handle);
  ModelHandle handle =
      model_instance_pool_.alloc(std::move(new_instance), model_instance_gpu_handle);

  if (handle.get_idx() >= model_to_resource_cache_key_.size()) {
    model_to_resource_cache_key_.resize(
        std::max<size_t>(model_instance_pool_.size(), model_to_resource_cache_key_.size() * 2));
  }
  model_to_resource_cache_key_[handle.get_idx()] = path_hash;

  return handle;
}

void ResourceManager::free_model(ModelHandle handle) {
  auto *model = model_instance_pool_.get(handle);
  ASSERT(model);
  auto it = model_cache_.find(model_to_resource_cache_key_[handle.get_idx()]);
  ASSERT(it != model_cache_.end());
  auto &entry = it->second;
  entry.use_count--;
  renderer_->free_instance(model->instance_gpu_handle);

  if (entry.use_count == 0) {
    renderer_->free_model(entry.gpu_resource_handle);
    model_cache_.erase(it);
  }
}

ResourceManager::ResourceManager(const CreateInfo &cinfo) : renderer_(cinfo.renderer) {}
