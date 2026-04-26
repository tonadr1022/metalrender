#include "gfx/renderer/MeshletResourceCompatibility.hpp"

#include <unordered_set>

#include "ResourceManager.hpp"
#include "engine/render/RenderScene.hpp"
#include "gfx/ModelInstance.hpp"

namespace teng::gfx {

void MeshletResourceCompatibility::sync(const engine::RenderScene& scene,
                                        const ResolveModelPathFn& resolve_model_path) {
  std::unordered_set<engine::EntityGuid> live_meshes;
  live_meshes.reserve(scene.meshes.size());

  for (const engine::RenderMesh& mesh : scene.meshes) {
    live_meshes.insert(mesh.entity);

    const auto existing = runtime_models_.find(mesh.entity);
    if (existing == runtime_models_.end() || existing->second.asset != mesh.model) {
      if (existing != runtime_models_.end()) {
        ResourceManager::get().free_model(existing->second.handle);
        runtime_models_.erase(existing);
      }
      const auto resolved_path = resolve_model_path(mesh.model);
      if (!resolved_path.has_value()) {
        continue;
      }
      runtime_models_.emplace(mesh.entity,
                              RuntimeModel{
                                  .handle = ResourceManager::get().load_model(*resolved_path,
                                                                               mesh.local_to_world),
                                  .asset = mesh.model,
                              });
      continue;
    }

    if (ModelInstance* model = ResourceManager::get().get_model(existing->second.handle)) {
      model->set_transform(0, mesh.local_to_world);
      model->update_transforms();
    }
  }

  for (auto it = runtime_models_.begin(); it != runtime_models_.end();) {
    if (live_meshes.contains(it->first)) {
      ++it;
      continue;
    }
    ResourceManager::get().free_model(it->second.handle);
    it = runtime_models_.erase(it);
  }
}

void MeshletResourceCompatibility::clear() {
  for (auto& [entity, model] : runtime_models_) {
    (void)entity;
    ResourceManager::get().free_model(model.handle);
  }
  runtime_models_.clear();
}

}  // namespace teng::gfx
