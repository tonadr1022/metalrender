#pragma once

#include <memory>

#include "engine/scene/ComponentRegistry.hpp"
#include "engine/scene/SceneComponentContext.hpp"
#include "engine/scene/SceneSerializationContext.hpp"

namespace teng::engine {

struct SceneTestContexts {
  std::unique_ptr<scene::ComponentRegistry> component_registry;
  FlecsComponentContext flecs_components;
  SceneSerializationContext scene_serialization;
};

[[nodiscard]] FlecsComponentContext make_scene_component_context();
[[nodiscard]] SceneSerializationContext make_scene_serialization_context();
[[nodiscard]] SceneTestContexts make_scene_test_contexts();

}  // namespace teng::engine
