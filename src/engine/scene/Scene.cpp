#include "engine/scene/Scene.hpp"

#include <flecs/addons/cpp/entity.hpp>
#include <flecs/addons/cpp/mixins/pipeline/decl.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <string>
#include <string_view>
#include <utility>

#include "core/EAssert.hpp"
#include "engine/Input.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneIds.hpp"

namespace teng::engine {

namespace {

[[nodiscard]] glm::vec3 camera_front(float yaw, float pitch) {
  glm::vec3 dir;
  dir.x = glm::cos(glm::radians(yaw)) * glm::cos(glm::radians(pitch));
  dir.y = glm::sin(glm::radians(pitch));
  dir.z = glm::sin(glm::radians(yaw)) * glm::cos(glm::radians(pitch));
  return glm::normalize(dir);
}

[[nodiscard]] bool any_key_down(const EngineInputSnapshot& input, int a, int b) {
  return input.key_down(a) || input.key_down(b);
}

[[nodiscard]] glm::mat4 camera_local_to_world(const glm::vec3& position, const glm::vec3& front) {
  return glm::inverse(glm::lookAt(position, position + front, glm::vec3{0.f, 1.f, 0.f}));
}

}  // namespace

Scene::Scene(SceneId id, std::string name) : id_(id), name_(std::move(name)) {
  ASSERT(id_.is_valid());
  register_components();
  register_systems();
}

Scene::~Scene() = default;

void Scene::register_components() {
  world_.component<EntityGuidComponent>("EntityGuidComponent");
  world_.component<Name>("Name");
  world_.component<Transform>("Transform");
  world_.component<LocalToWorld>("LocalToWorld");
  world_.component<Camera>("Camera");
  world_.component<FpsCameraController>("FpsCameraController");
  world_.component<EngineInputSnapshot>("EngineInputSnapshot");
  world_.component<DirectionalLight>("DirectionalLight");
  world_.component<MeshRenderable>("MeshRenderable");
  world_.component<SpriteRenderable>("SpriteRenderable");
}

void Scene::register_systems() {
  world_.system<Transform, LocalToWorld, FpsCameraController>("UpdateFpsCamera")
      .kind(flecs::OnUpdate)
      .each([this](Transform& transform, LocalToWorld& local_to_world,
                   FpsCameraController& controller) {
        const auto* input = world_.try_get<EngineInputSnapshot>();
        if (!input) {
          return;
        }

        if (input->key_pressed(KeyCode::Escape)) {
          controller.mouse_captured = !controller.mouse_captured;
        }

        if (controller.mouse_captured) {
          const glm::vec2 offset = input->cursor_delta * controller.mouse_sensitivity;
          controller.yaw += offset.x;
          controller.pitch += controller.look_pitch_sign * offset.y;
          controller.pitch = glm::clamp(controller.pitch, -89.f, 89.f);
        }

        if (!input->imgui_blocks_keyboard) {
          glm::vec3 acceleration{};
          bool accelerating{};
          const glm::vec3 front = camera_front(controller.yaw, controller.pitch);
          const glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3{0.f, 1.f, 0.f}));

          if (any_key_down(*input, KeyCode::W, KeyCode::I)) {
            acceleration += front;
            accelerating = true;
          }
          if (any_key_down(*input, KeyCode::S, KeyCode::K)) {
            acceleration -= front;
            accelerating = true;
          }
          if (any_key_down(*input, KeyCode::A, KeyCode::J)) {
            acceleration -= right;
            accelerating = true;
          }
          if (any_key_down(*input, KeyCode::D, KeyCode::L)) {
            acceleration += right;
            accelerating = true;
          }
          if (any_key_down(*input, KeyCode::Y, KeyCode::R)) {
            acceleration += glm::vec3{0.f, 1.f, 0.f};
            accelerating = true;
          }
          if (any_key_down(*input, KeyCode::H, KeyCode::F)) {
            acceleration -= glm::vec3{0.f, 1.f, 0.f};
            accelerating = true;
          }
          if (input->key_down(KeyCode::B)) {
            controller.move_speed *= 1.1f;
            controller.max_velocity *= 1.1f;
          }
          if (input->key_down(KeyCode::V)) {
            controller.move_speed /= 1.1f;
            controller.max_velocity /= 1.1f;
          }

          if (accelerating && glm::length(acceleration) > 0.0001f) {
            transform.translation += glm::normalize(acceleration) * controller.move_speed *
                                     controller.max_velocity * input->delta_seconds;
          }
        }

        const glm::vec3 front = camera_front(controller.yaw, controller.pitch);
        local_to_world.value = camera_local_to_world(transform.translation, front);
        transform.rotation = glm::quat_cast(local_to_world.value);
      });

  world_.system<const Transform, LocalToWorld>("UpdateLocalToWorld")
      .kind(flecs::OnUpdate)
      .each([](const Transform& transform, LocalToWorld& local_to_world) {
        local_to_world.value = transform_to_matrix(transform);
      });
}

flecs::entity Scene::create_entity(EntityGuid guid, std::string_view name) {
  ASSERT(guid.is_valid());
  ASSERT(!entities_by_guid_.contains(guid));

  const std::string entity_name{name};
  flecs::entity entity = entity_name.empty() ? world_.entity() : world_.entity(entity_name.c_str());
  entity.set<EntityGuidComponent>({.guid = guid});
  entity.set<Transform>({});
  entity.set<LocalToWorld>({});
  if (!entity_name.empty()) {
    entity.set<Name>({.value = entity_name});
  }

  entities_by_guid_.emplace(guid, entity);
  return entity;
}

void Scene::ensure_entity(EntityGuid guid, std::string_view name) {
  if (find_entity(guid).is_valid()) {
    return;
  }
  create_entity(guid, name);
}

void Scene::destroy_entity(EntityGuid guid) {
  const auto it = entities_by_guid_.find(guid);
  if (it == entities_by_guid_.end()) {
    return;
  }
  it->second.destruct();
  entities_by_guid_.erase(it);
}

bool Scene::has_entity(EntityGuid guid) const { return entities_by_guid_.contains(guid); }

flecs::entity Scene::find_entity(EntityGuid guid) const {
  const auto it = entities_by_guid_.find(guid);
  return it == entities_by_guid_.end() ? flecs::entity{} : it->second;
}

const LocalToWorld* Scene::get_local_to_world(EntityGuid guid) const {
  const flecs::entity entity = find_entity(guid);
  return entity.is_valid() ? entity.try_get<LocalToWorld>() : nullptr;
}

const FpsCameraController* Scene::get_fps_camera_controller(EntityGuid guid) const {
  const flecs::entity entity = find_entity(guid);
  return entity.is_valid() ? entity.try_get<FpsCameraController>() : nullptr;
}

// Compatibility authoring helpers mutate the Flecs world through an entity handle.
bool Scene::set_transform(EntityGuid guid, const Transform& transform) const {
  const flecs::entity entity = find_entity(guid);
  if (!entity.is_valid()) {
    return false;
  }
  entity.set<Transform>(transform);
  return true;
}

bool Scene::set_local_to_world(EntityGuid guid, const LocalToWorld& local_to_world) const {
  const flecs::entity entity = find_entity(guid);
  if (!entity.is_valid()) {
    return false;
  }
  entity.set<LocalToWorld>(local_to_world);
  return true;
}

bool Scene::set_camera(EntityGuid guid, const Camera& camera) const {
  const flecs::entity entity = find_entity(guid);
  if (!entity.is_valid()) {
    return false;
  }
  entity.set<Camera>(camera);
  return true;
}

bool Scene::set_fps_camera_controller(EntityGuid guid,
                                      const FpsCameraController& controller) const {
  const flecs::entity entity = find_entity(guid);
  if (!entity.is_valid()) {
    return false;
  }
  entity.set<FpsCameraController>(controller);
  return true;
}

void Scene::set_input_snapshot(const EngineInputSnapshot& input) {
  world_.set<EngineInputSnapshot>(input);
}

bool Scene::set_directional_light(EntityGuid guid, const DirectionalLight& light) const {
  const flecs::entity entity = find_entity(guid);
  if (!entity.is_valid()) {
    return false;
  }
  entity.set<DirectionalLight>(light);
  return true;
}

bool Scene::set_mesh_renderable(EntityGuid guid, const MeshRenderable& mesh) const {
  const flecs::entity entity = find_entity(guid);
  if (!entity.is_valid()) {
    return false;
  }
  entity.set<MeshRenderable>(mesh);
  return true;
}

bool Scene::tick(float delta_seconds) { return world_.progress(delta_seconds); }

}  // namespace teng::engine
