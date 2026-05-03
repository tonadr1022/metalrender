#include "SceneSerializationContext.hpp"

#include "core/ComponentRegistry.hpp"
#include "core/EAssert.hpp"

namespace teng::engine {

const core::ComponentRegistry& SceneSerializationContext::component_registry() const {
  ALWAYS_ASSERT(registry, "scene serialization context is missing component registry");
  return *registry;
}

const ComponentSerializationBinding* SceneSerializationContext::find_binding(
    std::string_view component_key) const {
  for (const ComponentSerializationBinding& binding : component_bindings) {
    if (binding.component_key == component_key) {
      return &binding;
    }
  }
  return nullptr;
}

void SceneSerializationContextBuilder::register_component(
    ComponentSerializationBinding component_binding) {
  component_bindings_.push_back(component_binding);
}

SceneSerializationContext SceneSerializationContextBuilder::freeze() const {
  SceneSerializationContext context;
  context.registry = &registry_;
  for (const ComponentSerializationBinding& binding : component_bindings_) {
    const auto* component = registry_.find(binding.component_key);
    ALWAYS_ASSERT(component, "component not found in registry for key {}", binding.component_key);
    if (component->storage == core::ComponentStoragePolicy::Authored) {
      ALWAYS_ASSERT(binding.has_component_fn, "has_component_fn is required for component key {}",
                    binding.component_key);
      ALWAYS_ASSERT(binding.serialize_fn, "serialize_fn is required for component key {}",
                    binding.component_key);
      ALWAYS_ASSERT(binding.deserialize_fn, "deserialize_fn is required for component key {}",
                    binding.component_key);
    }
    context.component_bindings.push_back(binding);
  }
  return context;
}

}  // namespace teng::engine
