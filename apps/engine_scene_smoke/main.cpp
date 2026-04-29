#include <iostream>

#include "DemoSceneEcsBridge.hpp"
#include "engine/assets/AssetDatabase.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/assets/AssetService.hpp"
#include "engine/scene/SceneSmokeTest.hpp"

int main() {
  if (!teng::engine::run_scene_foundation_smoke_test()) {
    std::cerr << "engine_scene_smoke: scene foundation smoke test failed\n";
    return 1;
  }
  if (!teng::engine::run_render_scene_extraction_smoke_test()) {
    std::cerr << "engine_scene_smoke: render scene extraction smoke test failed\n";
    return 1;
  }
  if (!teng::engine::run_fps_camera_system_smoke_test()) {
    std::cerr << "engine_scene_smoke: fps camera system smoke test failed\n";
    return 1;
  }
  if (!teng::gfx::demo_scene_compat::run_demo_scene_authoring_smoke_test()) {
    std::cerr << "engine_scene_smoke: demo scene authoring smoke test failed\n";
    return 1;
  }
  if (!teng::engine::assets::run_asset_registry_smoke_test()) {
    std::cerr << "engine_scene_smoke: asset registry smoke test failed\n";
    return 1;
  }
  if (!teng::engine::assets::run_asset_database_smoke_test()) {
    std::cerr << "engine_scene_smoke: asset database smoke test failed\n";
    return 1;
  }
  if (!teng::engine::assets::run_asset_service_smoke_test()) {
    std::cerr << "engine_scene_smoke: asset service smoke test failed\n";
    return 1;
  }
  return 0;
}
