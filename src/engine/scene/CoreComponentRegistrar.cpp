#include "engine/scene/CoreComponentRegistrar.hpp"

namespace TENG_NAMESPACE::engine {

namespace {

using core::ComponentFieldKind;
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
                                                    .default_on_create = true,
                                                    .fields = {
                                                        {.key = "translation",
                                                         .kind = ComponentFieldKind::Vec3,
                                                         .required = true,
                                                         .default_on_create = true},
                                                        {.key = "rotation",
                                                         .kind = ComponentFieldKind::Quat,
                                                         .required = true,
                                                         .default_on_create = true},
                                                        {.key = "scale",
                                                         .kind = ComponentFieldKind::Vec3,
                                                         .required = true,
                                                         .default_on_create = true},
                                                    }});

  register_component(builder, ComponentRegistration{.component_key = "teng.core.camera",
                                                    .module_id = "teng.core",
                                                    .module_version = 1,
                                                    .schema_version = 1,
                                                    .storage = ComponentStoragePolicy::Authored,
                                                    .default_on_create = true,
                                                    .fields = {
                                                        {.key = "pos",
                                                         .kind = ComponentFieldKind::Vec3,
                                                         .required = true,
                                                         .default_on_create = true},
                                                        {.key = "pitch",
                                                         .kind = ComponentFieldKind::F32,
                                                         .required = true,
                                                         .default_on_create = true},
                                                        {.key = "yaw",
                                                         .kind = ComponentFieldKind::F32,
                                                         .required = true,
                                                         .default_on_create = true},
                                                        {.key = "move_speed",
                                                         .kind = ComponentFieldKind::F32,
                                                         .required = true,
                                                         .default_on_create = true},
                                                    }});

  register_component(builder, ComponentRegistration{.component_key = "teng.core.directional_light",
                                                    .module_id = "teng.core",
                                                    .module_version = 1,
                                                    .schema_version = 1,
                                                    .storage = ComponentStoragePolicy::Authored,
                                                    .default_on_create = true,
                                                    .fields = {
                                                        {.key = "direction",
                                                         .kind = ComponentFieldKind::Vec3,
                                                         .required = true,
                                                         .default_on_create = true},
                                                        {.key = "color",
                                                         .kind = ComponentFieldKind::Vec3,
                                                         .required = true,
                                                         .default_on_create = true},
                                                        {.key = "intensity",
                                                         .kind = ComponentFieldKind::F32,
                                                         .required = true,
                                                         .default_on_create = true},
                                                    }});

  register_component(builder, ComponentRegistration{.component_key = "teng.core.mesh_renderable",
                                                    .module_id = "teng.core",
                                                    .module_version = 1,
                                                    .schema_version = 1,
                                                    .storage = ComponentStoragePolicy::Authored,
                                                    .default_on_create = true,
                                                    .fields = {
                                                        {.key = "model",
                                                         .kind = ComponentFieldKind::AssetId,
                                                         .required = true,
                                                         .default_on_create = true},
                                                    }});

  register_component(builder, ComponentRegistration{.component_key = "teng.core.sprite_renderable",
                                                    .module_id = "teng.core",
                                                    .module_version = 1,
                                                    .schema_version = 1,
                                                    .storage = ComponentStoragePolicy::Authored,
                                                    .fields = {
                                                        {.key = "texture",
                                                         .kind = ComponentFieldKind::AssetId,
                                                         .required = true,
                                                         .default_on_create = true},
                                                        {.key = "tint",
                                                         .kind = ComponentFieldKind::Vec4,
                                                         .required = true,
                                                         .default_on_create = true},
                                                        {.key = "sorting_layer",
                                                         .kind = ComponentFieldKind::I32,
                                                         .required = true,
                                                         .default_on_create = true},
                                                        {.key = "sorting_order",
                                                         .kind = ComponentFieldKind::I32,
                                                         .required = true,
                                                         .default_on_create = true},
                                                    }});

  register_component(builder,
                     ComponentRegistration{.component_key = "teng.core.local_to_world",
                                           .module_id = "teng.core",
                                           .module_version = 1,
                                           .schema_version = 1,
                                           .storage = ComponentStoragePolicy::RuntimeDerived,
                                           .fields = {
                                               {.key = "value",
                                                .kind = ComponentFieldKind::Mat4,
                                                .required = true,
                                                .default_on_create = true},
                                           }});

  register_component(builder,
                     ComponentRegistration{.component_key = "teng.core.fps_camera_controller",
                                           .module_id = "teng.core",
                                           .module_version = 1,
                                           .schema_version = 1,
                                           .storage = ComponentStoragePolicy::RuntimeSession,
                                           .fields = {
                                               {.key = "pitch",
                                                .kind = ComponentFieldKind::F32,
                                                .required = true,
                                                .default_on_create = true},
                                               {.key = "yaw",
                                                .kind = ComponentFieldKind::F32,
                                                .required = true,
                                                .default_on_create = true},
                                               {.key = "max_velocity",
                                                .kind = ComponentFieldKind::F32,
                                                .required = true,
                                                .default_on_create = true},
                                               {.key = "move_speed",
                                                .kind = ComponentFieldKind::F32,
                                                .required = true,
                                                .default_on_create = true},
                                               {.key = "mouse_sensitivity",
                                                .kind = ComponentFieldKind::F32,
                                                .required = true,
                                                .default_on_create = true},
                                               {.key = "look_pitch_sign",
                                                .kind = ComponentFieldKind::F32,
                                                .required = true,
                                                .default_on_create = true},
                                               {.key = "mouse_captured",
                                                .kind = ComponentFieldKind::Bool,
                                                .required = true,
                                                .default_on_create = true},
                                           }});

  register_component(builder,
                     ComponentRegistration{.component_key = "teng.core.engine_input_snapshot",
                                           .module_id = "teng.core",
                                           .module_version = 1,
                                           .schema_version = 1,
                                           .storage = ComponentStoragePolicy::RuntimeSession});
}

}  // namespace TENG_NAMESPACE::engine
