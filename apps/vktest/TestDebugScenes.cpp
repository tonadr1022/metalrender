#include "TestDebugScenes.hpp"

#include <GLFW/glfw3.h>

#include <cstring>
#include <glm/gtc/matrix_transform.hpp>

#include "core/EAssert.hpp"
#include "scenes/MeshletRendererTestScene.hpp"

using namespace teng;
using namespace teng::gfx;
using namespace teng::gfx::rhi;

namespace teng::gfx {

const char* to_string(TestDebugScene s) {
  switch (s) {
    case TestDebugScene::MeshletRenderer:
      return "MeshletRenderer";
    case TestDebugScene::Count:
    default:
      return "Invalid";
  }
}

std::unique_ptr<ITestScene> create_test_scene(TestDebugScene s, const TestSceneContext& ctx) {
  switch (s) {
    case TestDebugScene::MeshletRenderer:
      return std::make_unique<MeshletRendererScene>(ctx);
    case TestDebugScene::Count:
    default:
      ASSERT(0);
      return nullptr;
  }
}

}  // namespace teng::gfx
