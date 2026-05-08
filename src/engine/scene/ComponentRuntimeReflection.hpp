#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "engine/scene/ComponentRegistry.hpp"
#include "engine/scene/SceneComponentContext.hpp"
#include "engine/scene/SceneSerializationContext.hpp"

namespace teng::engine::scene {

enum class ScriptExposure : uint8_t {
  None,
  Read,
  ReadWrite,
};

struct ReflectedFieldRecord {
  std::string_view key;
  std::string_view member_name;
  ComponentFieldKind kind{};
  bool authored_required{true};
  ComponentFieldDefaultValue default_value;
  std::optional<ComponentAssetFieldMetadata> asset;
  std::optional<ComponentEnumRegistration> enumeration;
  ScriptExposure script_exposure{ScriptExposure::None};
};

struct ReflectedComponentRecord {
  std::string_view component_key;
  std::string_view module_id;
  uint32_t module_version{1};
  uint32_t schema_version{1};
  ComponentStoragePolicy storage{ComponentStoragePolicy::Authored};
  ComponentSchemaVisibility visibility{ComponentSchemaVisibility::Editable};
  bool add_on_create{};
  ComponentSchemaValidationHook schema_validation_hook{nullptr};
  std::span<const ReflectedFieldRecord> fields;
};

}  // namespace teng::engine::scene

namespace teng::engine {

struct ReflectedFlecsThunks {
  std::string_view component_key;
  RegisterFlecsFn register_flecs_fn{};
  ApplyOnCreateFn apply_on_create_fn{};
};

struct ReflectedSerializationThunks {
  std::string_view component_key;
  HasComponentFn has_component_fn{};
  SerializeComponentFn serialize_fn{};
  DeserializeComponentFn deserialize_fn{};
};

void register_reflected_components(scene::ComponentRegistryBuilder& builder,
                                   std::span<const scene::ReflectedComponentRecord> components);

[[maybe_unused]] void register_reflected_flecs(const scene::ComponentRegistry& registry,
                                               FlecsComponentContextBuilder& builder,
                                               std::span<const ReflectedFlecsThunks> thunks);

[[maybe_unused]] void register_reflected_serialization(
    SceneSerializationContextBuilder& builder,
    std::span<const ReflectedSerializationThunks> thunks);

}  // namespace teng::engine
