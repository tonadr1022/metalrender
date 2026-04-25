#include <iostream>

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
  return 0;
}
