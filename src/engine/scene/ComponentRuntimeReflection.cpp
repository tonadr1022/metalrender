#include "engine/scene/ComponentRuntimeReflection.hpp"

#include <string>
#include <unordered_map>
#include <vector>

#include "core/EAssert.hpp"

namespace teng::engine {

namespace {

[[nodiscard]] scene::ComponentFieldRegistration copy_field_record(
    const scene::ReflectedFieldRecord& field) {
  return scene::ComponentFieldRegistration{
      .key = std::string{field.key},
      .kind = field.kind,
      .authored_required = field.authored_required,
      .default_value = field.default_value,
      .asset = field.asset,
      .enumeration = field.enumeration,
  };
}

}  // namespace

void register_reflected_components(  // NOLINT(misc-use-internal-linkage)
    scene::ComponentRegistryBuilder& builder,
    std::span<const scene::ReflectedComponentRecord> components) {
  std::unordered_map<std::string, uint32_t> registered_modules;

  for (const scene::ReflectedComponentRecord& reflected : components) {
    const auto module_it = registered_modules.find(std::string{reflected.module_id});
    if (module_it == registered_modules.end()) {
      registered_modules.emplace(reflected.module_id, reflected.module_version);
      builder.register_module(std::string{reflected.module_id}, reflected.module_version);
    } else if (module_it->second != reflected.module_version) {
      builder.register_module(std::string{reflected.module_id}, reflected.module_version);
    }

    std::vector<scene::ComponentFieldRegistration> fields;
    fields.reserve(reflected.fields.size());
    for (const scene::ReflectedFieldRecord& field : reflected.fields) {
      fields.push_back(copy_field_record(field));
    }

    builder.register_component(scene::ComponentRegistration{
        .component_key = std::string{reflected.component_key},
        .module_id = std::string{reflected.module_id},
        .module_version = reflected.module_version,
        .schema_version = reflected.schema_version,
        .storage = reflected.storage,
        .visibility = reflected.visibility,
        .add_on_create = reflected.add_on_create,
        .schema_validation_hook = reflected.schema_validation_hook,
        .fields = std::move(fields),
    });
  }
}

void register_reflected_flecs(  // NOLINT(misc-use-internal-linkage)
    const scene::ComponentRegistry& registry, FlecsComponentContextBuilder& builder,
    std::span<const ReflectedFlecsThunks> thunks) {
  for (const ReflectedFlecsThunks& thunk : thunks) {
    ASSERT(registry.find(thunk.component_key),
           "reflected Flecs thunk references unknown component {}", thunk.component_key);
    ASSERT(thunk.register_flecs_fn, "reflected Flecs thunk missing register function for {}",
           thunk.component_key);
    builder.register_flecs_component(FlecsComponentBinding{
        .component_key = thunk.component_key,
        .register_flecs_fn = thunk.register_flecs_fn,
        .apply_on_create_fn = thunk.apply_on_create_fn,
    });
  }
}

void register_reflected_serialization(  // NOLINT(misc-use-internal-linkage)
    SceneSerializationContextBuilder& builder,
    std::span<const ReflectedSerializationThunks> thunks) {
  for (const ReflectedSerializationThunks& thunk : thunks) {
    ASSERT(thunk.has_component_fn, "reflected serialization thunk missing has function for {}",
           thunk.component_key);
    ASSERT(thunk.serialize_fn, "reflected serialization thunk missing serialize function for {}",
           thunk.component_key);
    ASSERT(thunk.deserialize_fn,
           "reflected serialization thunk missing deserialize function for {}",
           thunk.component_key);
    builder.register_component(ComponentSerializationBinding{
        .component_key = thunk.component_key,
        .has_component_fn = thunk.has_component_fn,
        .serialize_fn = thunk.serialize_fn,
        .deserialize_fn = thunk.deserialize_fn,
    });
  }
}

}  // namespace teng::engine
