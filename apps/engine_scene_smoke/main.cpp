#include <iostream>

#include "AssetDatabaseSmokeTest.hpp"
#include "AssetRegistrySmokeTest.hpp"
#include "AssetServiceSmokeTest.hpp"
#include "GeneratedSceneAssetsSmokeTest.hpp"
#include "SceneSerializationSmokeTest.hpp"
#include "SceneSmokeTest.hpp"

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
  if (!teng::engine::run_scene_serialization_smoke_test()) {
    std::cerr << "engine_scene_smoke: scene serialization smoke test failed\n";
    return 1;
  }
  if (!teng::engine::run_generated_scene_assets_smoke_test()) {
    std::cerr << "engine_scene_smoke: generated scene assets smoke test failed\n";
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
