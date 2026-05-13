#pragma once

#include <flecs.h>

#include <nlohmann/json_fwd.hpp>
#include <string_view>

#include "engine/scene/ComponentRegistry.hpp"

namespace teng {

namespace engine::scene {
class ComponentRegistry;
struct FrozenComponentRecord;
}  // namespace engine::scene

namespace engine {

using SerializeComponentFn = scene::SerializeComponentFn;
using DeserializeComponentFn = scene::DeserializeComponentFn;
using HasComponentFn = scene::HasComponentFn;

struct SceneSerializationContext {
  const scene::ComponentRegistry* registry{};

  [[nodiscard]] const scene::ComponentRegistry& component_registry() const;
  [[nodiscard]] const scene::FrozenComponentRecord* find_authored_component(
      std::string_view component_key) const;
};

[[nodiscard]] SceneSerializationContext make_scene_serialization_context(
    const scene::ComponentRegistry& registry);

}  // namespace engine
}  // namespace teng
