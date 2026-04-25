#pragma once

#include "engine/render/RenderScene.hpp"

namespace teng::engine {

class Scene;

struct RenderSceneExtractStats {
  uint32_t skipped_meshes_missing_asset{};
  uint32_t skipped_sprites_missing_asset{};
};

struct RenderSceneExtractOptions {
  RenderSceneFrame frame;
  RenderSceneExtractStats* stats{};
};

[[nodiscard]] RenderScene extract_render_scene(Scene& scene,
                                               const RenderSceneExtractOptions& options = {});

}  // namespace teng::engine
