#pragma once

#include <flecs.h>

#include <nlohmann/json_fwd.hpp>
#include <string_view>
#include <vector>

namespace teng {

namespace engine::scene {
class ComponentRegistry;
struct FrozenComponentRecord;
}  // namespace engine::scene

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
  const scene::ComponentRegistry* registry{};
  std::vector<ComponentSerializationBinding> component_bindings;

  [[nodiscard]] const scene::ComponentRegistry& component_registry() const;
  [[nodiscard]] const ComponentSerializationBinding* find_binding(
      std::string_view component_key) const;
};

class SceneSerializationContextBuilder {
 public:
  explicit SceneSerializationContextBuilder(const scene::ComponentRegistry& registry)
      : registry_(registry) {}

  void register_component(ComponentSerializationBinding component_binding);
  [[nodiscard]] SceneSerializationContext freeze() const;

 private:
  const scene::ComponentRegistry& registry_;
  std::vector<ComponentSerializationBinding> component_bindings_;
};

}  // namespace engine
}  // namespace teng
