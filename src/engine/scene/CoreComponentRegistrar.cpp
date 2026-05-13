#include "engine/scene/CoreComponentRegistrar.hpp"

namespace teng::engine {

namespace scene {
struct ComponentModuleDescriptor;
}  // namespace scene

namespace core_component_generated {
std::span<const scene::ComponentModuleDescriptor> core_components_modules();
}  // namespace core_component_generated

std::span<const scene::ComponentModuleDescriptor> core_component_modules() {
  return core_component_generated::core_components_modules();
}

}  // namespace teng::engine
