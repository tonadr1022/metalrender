#pragma once

#include <flecs.h>

#include <nlohmann/json_fwd.hpp>

namespace teng {

namespace core {
class ComponentRegistry;
}  // namespace core

namespace engine {

using SerializeComponentFn = nlohmann::json (*)(flecs::entity entity);
using DeserializeComponent = void (*)(flecs::entity entity, const nlohmann::json& payload);
using HasComponentFn = bool (*)(flecs::entity entity);

struct ComponentSerializationBinding {
  std::string_view component_key;
  HasComponentFn has_component_fn{};
  SerializeComponentFn serialize_fn{};
  DeserializeComponent deserialize_fn{};
};

struct SceneSerializationContext {
  std::vector<ComponentSerializationBinding> component_bindings;
};

class SceneSerializationContextBuilder {
 public:
  explicit SceneSerializationContextBuilder(const core::ComponentRegistry& registry)
      : registry_(registry) {}

  void register_component(ComponentSerializationBinding component_binding);
  [[nodiscard]] SceneSerializationContext freeze() const;

 private:
  const core::ComponentRegistry& registry_;
  std::vector<ComponentSerializationBinding> component_bindings_;
};

}  // namespace engine
}  // namespace teng