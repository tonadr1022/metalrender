#include "TestExtensionComponent.hpp"

#include "test_extension_reflect.generated.hpp"

namespace teng::engine {

void register_test_extension_components(scene::ComponentRegistryBuilder& builder) {
  test_extension_generated::register_test_extension_reflected_components(builder);
}

void register_flecs_test_extension_components(FlecsComponentContextBuilder& builder) {
  test_extension_generated::register_test_extension_reflected_flecs(builder.registry(), builder);
}

void register_test_extension_serialization(SceneSerializationContextBuilder& builder) {
  test_extension_generated::register_test_extension_reflected_serialization(builder);
}

}  // namespace teng::engine
