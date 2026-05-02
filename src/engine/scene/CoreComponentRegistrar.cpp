#include "engine/scene/CoreComponentRegistrar.hpp"

#include "core/ComponentRegistry.hpp"
#include "engine/Input.hpp"
#include "engine/scene/SceneComponents.hpp"

namespace TENG_NAMESPACE::engine {

namespace {

using core::ComponentAssetFieldMetadata;
using core::ComponentDefaultAssetId;
using core::ComponentDefaultMat4;
using core::ComponentDefaultQuat;
using core::ComponentDefaultVec3;
using core::ComponentDefaultVec4;
using core::ComponentFieldDefaultValue;
using core::ComponentFieldKind;
using core::ComponentRegistration;
using core::ComponentSchemaVisibility;
using core::ComponentStoragePolicy;

[[nodiscard]] ComponentDefaultMat4 mat4_identity_default() {
  ComponentDefaultMat4 m{};
  m.elements[0] = 1.f;
  m.elements[5] = 1.f;
  m.elements[10] = 1.f;
  m.elements[15] = 1.f;
  return m;
}

void register_component(SceneComponentContextBuilder& builder, ComponentRegistration registration,
                        FlecsComponentBinding flecs_binding) {
  builder.register_component(std::move(registration), flecs_binding);
}

}  // namespace

void register_core_scene_component_bindings(SceneComponentContextBuilder& builder) {
  builder.registry_builder().register_module("teng.core", 1);

  register_component(
      builder,
      ComponentRegistration{
          .component_key = "teng.core.transform",
          .module_id = "teng.core",
          .module_version = 1,
          .schema_version = 1,
          .storage = ComponentStoragePolicy::Authored,
          .visibility = ComponentSchemaVisibility::Editable,
          .add_on_create = true,
          .fields =
              {
                  {.key = "translation",
                   .kind = ComponentFieldKind::Vec3,
                   .authored_required = true,
                   .default_value =
                       ComponentFieldDefaultValue{ComponentDefaultVec3{0.f, 0.f, 0.f}}},
                  {.key = "rotation",
                   .kind = ComponentFieldKind::Quat,
                   .authored_required = true,
                   .default_value =
                       ComponentFieldDefaultValue{ComponentDefaultQuat{1.f, 0.f, 0.f, 0.f}}},
                  {.key = "scale",
                   .kind = ComponentFieldKind::Vec3,
                   .authored_required = true,
                   .default_value =
                       ComponentFieldDefaultValue{ComponentDefaultVec3{1.f, 1.f, 1.f}}},
              },
      },
      FlecsComponentBinding{
          .register_flecs_fn = [](flecs::world& world) { world.component<Transform>(); },
          .apply_on_create_fn =
              [](flecs::entity entity) {
                entity.set<Transform>({.translation = {0.f, 0.f, 0.f},
                                       .rotation = {1.f, 0.f, 0.f, 0.f},
                                       .scale = {1.f, 1.f, 1.f}});
              },
      });

  register_component(
      builder,
      ComponentRegistration{
          .component_key = "teng.core.camera",
          .module_id = "teng.core",
          .module_version = 1,
          .schema_version = 1,
          .storage = ComponentStoragePolicy::Authored,
          .visibility = ComponentSchemaVisibility::Editable,
          .add_on_create = false,
          .fields =
              {
                  {.key = "fov_y",
                   .kind = ComponentFieldKind::F32,
                   .authored_required = true,
                   .default_value = ComponentFieldDefaultValue{1.04719755f}},
                  {.key = "z_near",
                   .kind = ComponentFieldKind::F32,
                   .authored_required = true,
                   .default_value = ComponentFieldDefaultValue{0.1f}},
                  {.key = "z_far",
                   .kind = ComponentFieldKind::F32,
                   .authored_required = true,
                   .default_value = ComponentFieldDefaultValue{10000.f}},
                  {.key = "primary",
                   .kind = ComponentFieldKind::Bool,
                   .authored_required = true,
                   .default_value = ComponentFieldDefaultValue{false}},
              },
      },
      FlecsComponentBinding{
          .register_flecs_fn = [](flecs::world& world) { world.component<Camera>(); },
          .apply_on_create_fn =
              [](flecs::entity entity) {
                entity.set<Camera>(
                    {.fov_y = 1.04719755f, .z_near = 0.1f, .z_far = 10000.f, .primary = false});
              },
      });

  register_component(
      builder,
      ComponentRegistration{
          .component_key = "teng.core.directional_light",
          .module_id = "teng.core",
          .module_version = 1,
          .schema_version = 1,
          .storage = ComponentStoragePolicy::Authored,
          .visibility = ComponentSchemaVisibility::Editable,
          .add_on_create = false,
          .fields =
              {
                  {.key = "direction",
                   .kind = ComponentFieldKind::Vec3,
                   .authored_required = true,
                   .default_value =
                       ComponentFieldDefaultValue{ComponentDefaultVec3{0.35f, 1.f, 0.4f}}},
                  {.key = "color",
                   .kind = ComponentFieldKind::Vec3,
                   .authored_required = true,
                   .default_value =
                       ComponentFieldDefaultValue{ComponentDefaultVec3{1.f, 1.f, 1.f}}},
                  {.key = "intensity",
                   .kind = ComponentFieldKind::F32,
                   .authored_required = true,
                   .default_value = ComponentFieldDefaultValue{1.f}},
              },
      },
      FlecsComponentBinding{
          .register_flecs_fn = [](flecs::world& world) { world.component<DirectionalLight>(); },
          .apply_on_create_fn =
              [](flecs::entity entity) {
                entity.set<DirectionalLight>(
                    {.direction = {0.35f, 1.f, 0.4f}, .color = {1.f, 1.f, 1.f}, .intensity = 1.f});
              },
      });

  register_component(
      builder,
      ComponentRegistration{
          .component_key = "teng.core.mesh_renderable",
          .module_id = "teng.core",
          .module_version = 1,
          .schema_version = 1,
          .storage = ComponentStoragePolicy::Authored,
          .visibility = ComponentSchemaVisibility::Editable,
          .add_on_create = false,
          .fields =
              {
                  {.key = "model",
                   .kind = ComponentFieldKind::AssetId,
                   .authored_required = true,
                   .default_value = ComponentFieldDefaultValue{ComponentDefaultAssetId{}},
                   .asset = ComponentAssetFieldMetadata{.expected_kind = "model"}},
              },
      },
      FlecsComponentBinding{
          .register_flecs_fn = [](flecs::world& world) { world.component<MeshRenderable>(); },
          .apply_on_create_fn =
              [](flecs::entity entity) { entity.set<MeshRenderable>({.model = AssetId{}}); },
      });

  register_component(
      builder,
      ComponentRegistration{
          .component_key = "teng.core.sprite_renderable",
          .module_id = "teng.core",
          .module_version = 1,
          .schema_version = 1,
          .storage = ComponentStoragePolicy::Authored,
          .visibility = ComponentSchemaVisibility::Editable,
          .add_on_create = false,
          .fields =
              {
                  {.key = "texture",
                   .kind = ComponentFieldKind::AssetId,
                   .authored_required = true,
                   .default_value = ComponentFieldDefaultValue{ComponentDefaultAssetId{}},
                   .asset = ComponentAssetFieldMetadata{.expected_kind = "texture"}},
                  {.key = "tint",
                   .kind = ComponentFieldKind::Vec4,
                   .authored_required = true,
                   .default_value =
                       ComponentFieldDefaultValue{ComponentDefaultVec4{1.f, 1.f, 1.f, 1.f}}},
                  {.key = "sorting_layer",
                   .kind = ComponentFieldKind::I32,
                   .authored_required = true,
                   .default_value = ComponentFieldDefaultValue{int64_t{0}}},
                  {.key = "sorting_order",
                   .kind = ComponentFieldKind::I32,
                   .authored_required = true,
                   .default_value = ComponentFieldDefaultValue{int64_t{0}}},
              },
      },
      FlecsComponentBinding{
          .register_flecs_fn = [](flecs::world& world) { world.component<SpriteRenderable>(); },
          .apply_on_create_fn =
              [](flecs::entity entity) {
                entity.set<SpriteRenderable>({.texture = AssetId{},
                                              .tint = {1.f, 1.f, 1.f, 1.f},
                                              .sorting_layer = 0,
                                              .sorting_order = 0});
              },
      });

  register_component(
      builder,
      ComponentRegistration{
          .component_key = "teng.core.local_to_world",
          .module_id = "teng.core",
          .module_version = 1,
          .schema_version = 1,
          .storage = ComponentStoragePolicy::RuntimeDerived,
          .visibility = ComponentSchemaVisibility::DebugInspectable,
          .add_on_create = true,
          .fields =
              {
                  {.key = "value",
                   .kind = ComponentFieldKind::Mat4,
                   .authored_required = true,
                   .default_value = ComponentFieldDefaultValue{mat4_identity_default()}},
              },
      },
      FlecsComponentBinding{
          .register_flecs_fn = [](flecs::world& world) { world.component<LocalToWorld>(); },
          .apply_on_create_fn =
              [](flecs::entity entity) { entity.set<LocalToWorld>({.value = glm::mat4(1.f)}); },
      });

  register_component(
      builder,
      ComponentRegistration{.component_key = "teng.core.fps_camera_controller",
                            .module_id = "teng.core",
                            .module_version = 1,
                            .schema_version = 1,
                            .storage = ComponentStoragePolicy::RuntimeSession,
                            .visibility = ComponentSchemaVisibility::DebugInspectable,
                            .add_on_create = false,
                            .fields =
                                {
                                    {.key = "pitch",
                                     .kind = ComponentFieldKind::F32,
                                     .authored_required = true,
                                     .default_value = ComponentFieldDefaultValue{0.f}},
                                    {.key = "yaw",
                                     .kind = ComponentFieldKind::F32,
                                     .authored_required = true,
                                     .default_value = ComponentFieldDefaultValue{0.f}},
                                    {.key = "max_velocity",
                                     .kind = ComponentFieldKind::F32,
                                     .authored_required = true,
                                     .default_value = ComponentFieldDefaultValue{5.f}},
                                    {.key = "move_speed",
                                     .kind = ComponentFieldKind::F32,
                                     .authored_required = true,
                                     .default_value = ComponentFieldDefaultValue{10.f}},
                                    {.key = "mouse_sensitivity",
                                     .kind = ComponentFieldKind::F32,
                                     .authored_required = true,
                                     .default_value = ComponentFieldDefaultValue{0.1f}},
                                    {.key = "look_pitch_sign",
                                     .kind = ComponentFieldKind::F32,
                                     .authored_required = true,
                                     .default_value = ComponentFieldDefaultValue{1.f}},
                                    {.key = "mouse_captured",
                                     .kind = ComponentFieldKind::Bool,
                                     .authored_required = true,
                                     .default_value = ComponentFieldDefaultValue{false}},
                                }},
      FlecsComponentBinding{
          .register_flecs_fn = [](flecs::world& world) { world.component<FpsCameraController>(); },
          .apply_on_create_fn =
              [](flecs::entity entity) {
                entity.set<FpsCameraController>({.pitch = 0.f,
                                                 .yaw = 0.f,
                                                 .max_velocity = 5.f,
                                                 .move_speed = 10.f,
                                                 .mouse_sensitivity = 0.1f,
                                                 .look_pitch_sign = 1.f,
                                                 .mouse_captured = false});
              },
      });

  register_component(
      builder,
      ComponentRegistration{.component_key = "teng.core.engine_input_snapshot",
                            .module_id = "teng.core",
                            .module_version = 1,
                            .schema_version = 1,
                            .storage = ComponentStoragePolicy::RuntimeSession,
                            .visibility = ComponentSchemaVisibility::Hidden,
                            .add_on_create = false,
                            .fields = {}},
      FlecsComponentBinding{
          .register_flecs_fn = [](flecs::world& world) { world.component<EngineInputSnapshot>(); },
          .apply_on_create_fn = [](flecs::entity entity) { entity.set<EngineInputSnapshot>({}); },
      });
}

}  // namespace TENG_NAMESPACE::engine
