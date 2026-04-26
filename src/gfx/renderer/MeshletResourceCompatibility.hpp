#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <unordered_map>

#include "engine/scene/SceneIds.hpp"
#include "gfx/RendererTypes.hpp"

namespace teng::engine {
struct RenderScene;
}

namespace teng::gfx {

class MeshletResourceCompatibility {
 public:
  using ResolveModelPathFn =
      std::function<std::optional<std::filesystem::path>(engine::AssetId)>;

  void sync(const engine::RenderScene& scene, const ResolveModelPathFn& resolve_model_path);
  void clear();

 private:
  struct RuntimeModel {
    ModelHandle handle;
    engine::AssetId asset;
  };

  std::unordered_map<engine::EntityGuid, RuntimeModel> runtime_models_;
};

}  // namespace teng::gfx
