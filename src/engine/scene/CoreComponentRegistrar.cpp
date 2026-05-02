#include "engine/scene/CoreComponentRegistrar.hpp"

namespace TENG_NAMESPACE::engine {

namespace {

using core::ComponentRegistration;
using core::ComponentStoragePolicy;

void register_component(core::ComponentRegistryBuilder& builder,
                        ComponentRegistration registration) {
  builder.register_component(std::move(registration));
}

}  // namespace

void register_core_components(core::ComponentRegistryBuilder& builder) {
  builder.register_module("teng.core", 1);

  register_component(builder, ComponentRegistration{.component_key = "teng.core.transform",
                                                    .module_id = "teng.core",
                                                    .module_version = 1,
                                                    .schema_version = 1,
                                                    .storage = ComponentStoragePolicy::Authored,
                                                    .default_on_create = true});

  register_component(builder, ComponentRegistration{.component_key = "teng.core.camera",
                                                    .module_id = "teng.core",
                                                    .module_version = 1,
                                                    .schema_version = 1,
                                                    .storage = ComponentStoragePolicy::Authored});

  register_component(builder, ComponentRegistration{.component_key = "teng.core.directional_light",
                                                    .module_id = "teng.core",
                                                    .module_version = 1,
                                                    .schema_version = 1,
                                                    .storage = ComponentStoragePolicy::Authored});

  register_component(builder, ComponentRegistration{.component_key = "teng.core.mesh_renderable",
                                                    .module_id = "teng.core",
                                                    .module_version = 1,
                                                    .schema_version = 1,
                                                    .storage = ComponentStoragePolicy::Authored});

  register_component(builder, ComponentRegistration{.component_key = "teng.core.sprite_renderable",
                                                    .module_id = "teng.core",
                                                    .module_version = 1,
                                                    .schema_version = 1,
                                                    .storage = ComponentStoragePolicy::Authored});

  register_component(builder,
                     ComponentRegistration{.component_key = "teng.core.local_to_world",
                                           .module_id = "teng.core",
                                           .module_version = 1,
                                           .schema_version = 1,
                                           .storage = ComponentStoragePolicy::RuntimeDerived});

  register_component(builder,
                     ComponentRegistration{.component_key = "teng.core.fps_camera_controller",
                                           .module_id = "teng.core",
                                           .module_version = 1,
                                           .schema_version = 1,
                                           .storage = ComponentStoragePolicy::RuntimeSession});

  register_component(builder,
                     ComponentRegistration{.component_key = "teng.core.engine_input_snapshot",
                                           .module_id = "teng.core",
                                           .module_version = 1,
                                           .schema_version = 1,
                                           .storage = ComponentStoragePolicy::RuntimeSession});
}

}  // namespace TENG_NAMESPACE::engine
