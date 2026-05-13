#include "SceneSerializationContext.hpp"

#include "core/EAssert.hpp"
#include "engine/scene/ComponentRegistry.hpp"

namespace teng::engine {

const scene::ComponentRegistry& SceneSerializationContext::component_registry() const {
  ALWAYS_ASSERT(registry, "scene serialization context is missing component registry");
  return *registry;
}

const scene::FrozenComponentRecord* SceneSerializationContext::find_authored_component(
    std::string_view component_key) const {
  const scene::FrozenComponentRecord* component = component_registry().find(component_key);
  if (!component || component->storage != scene::ComponentStoragePolicy::Authored) {
    return nullptr;
  }
  return component;
}

SceneSerializationContext make_scene_serialization_context(
    const scene::ComponentRegistry& registry) {
  return SceneSerializationContext{.registry = &registry};
}

}  // namespace teng::engine
