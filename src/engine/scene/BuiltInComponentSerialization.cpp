namespace teng::engine {

class SceneSerializationContextBuilder;

namespace core_component_generated {
void register_core_components_reflected_serialization(SceneSerializationContextBuilder& builder);
}  // namespace core_component_generated

void register_builtin_component_serialization(  // NOLINT(misc-use-internal-linkage)
    SceneSerializationContextBuilder& builder) {
  core_component_generated::register_core_components_reflected_serialization(builder);
}

}  // namespace teng::engine
