#pragma once
#include <cassert>
#include <filesystem>
#include <glm/mat4x4.hpp>

#include "core/Handle.hpp"
#include "core/Pool.hpp"
#include "gfx/ModelInstance.hpp"
#include "gfx/RendererTypes.hpp"

namespace gfx {
class MemeRenderer123;
}

class ResourceManager {
 public:
  struct CreateInfo {
    gfx::MemeRenderer123 *renderer;
  };

 private:
  explicit ResourceManager(const CreateInfo &cinfo);

 public:
  static void init(const CreateInfo &cinfo) {
    assert(!instance_);
    instance_ = new ResourceManager{cinfo};
  }
  static void shutdown() {
    delete instance_;
    instance_ = nullptr;
  }
  static ResourceManager &get() {
    assert(instance_);
    return *instance_;
  }

  [[nodiscard]] size_t get_tot_models_loaded() const { return tot_models_loaded_; }
  [[nodiscard]] size_t get_tot_instances_loaded() const { return tot_instances_loaded_; }

  ModelInstance *get_model(ModelHandle handle) {
    auto *entry = model_instance_pool_.get(handle);
    return entry ? &entry->instance : nullptr;
  }

  ModelHandle load_model(const std::filesystem::path &path,
                         const glm::mat4 &root_transform = glm::mat4{1});
  void free_model(ModelHandle handle);

 private:
  struct ModelCacheEntry {
    ModelInstance model;
    ModelGPUHandle gpu_resource_handle;
    size_t use_count{};
  };

  std::unordered_map<size_t, ModelCacheEntry> model_cache_;

  struct ModelInstancePoolEntry {
    ModelInstance instance;
    ModelInstanceGPUHandle instance_gpu_handle;
  };
  Pool<ModelHandle, ModelInstancePoolEntry> model_instance_pool_;
  std::vector<size_t> model_to_resource_cache_key_;

  gfx::MemeRenderer123 *renderer_{};
  inline static ResourceManager *instance_{};
  size_t tot_models_loaded_{0};
  size_t tot_instances_loaded_{0};
};
