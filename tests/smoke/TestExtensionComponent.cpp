#include "TestExtensionComponent.hpp"

#include "test_extension_reflect.generated.hpp"

namespace teng::engine {

std::span<const scene::ComponentModuleDescriptor> test_extension_component_modules() {
  return test_extension_generated::test_extension_modules();
}

}  // namespace teng::engine
