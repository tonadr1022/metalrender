#pragma once

#include <filesystem>
#include <string>

#include "core/Result.hpp"
#include "engine/scene/SceneIds.hpp"
#include "engine/scene/SceneManager.hpp"

namespace teng::engine {

struct SceneAssetLoadResult {
  SceneId scene_id;
  Scene* scene{};
};

[[nodiscard]] Result<SceneAssetLoadResult> load_scene_asset(SceneManager& scenes,
                                                            const std::filesystem::path& path);

}  // namespace teng::engine
