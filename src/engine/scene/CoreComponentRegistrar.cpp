namespace teng::engine {

class FlecsComponentContextBuilder;

namespace scene {
class ComponentRegistryBuilder;
}  // namespace scene

namespace core_component_generated {
void register_core_components_reflected_components(scene::ComponentRegistryBuilder& builder);
void register_core_components_reflected_flecs(FlecsComponentContextBuilder& builder);
}  // namespace core_component_generated

void register_core_components(  // NOLINT(misc-use-internal-linkage)
    scene::ComponentRegistryBuilder& builder) {
  core_component_generated::register_core_components_reflected_components(builder);
}

void register_flecs_core_components(  // NOLINT(misc-use-internal-linkage)
    FlecsComponentContextBuilder& builder) {
  core_component_generated::register_core_components_reflected_flecs(builder);
}

}  // namespace teng::engine
