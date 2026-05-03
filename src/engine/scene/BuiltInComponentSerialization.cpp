#include "BuiltinComponentSerialization.hpp"

#include <flecs.h>

#include <nlohmann/json.hpp>

#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneSerializationContext.hpp"

namespace teng::engine {

using json = nlohmann::json;

void register_builtin_component_serialization(SceneSerializationContextBuilder& builder) {
  builder.register_component({
      .component_key = "teng.core.transform",
      .has_component_fn = [](flecs::entity entity) { return entity.has<Transform>(); },
      .serialize_fn = [](flecs::entity entity) -> json {
        const auto& transform = entity.get<Transform>();
        const auto& translation = transform.translation;
        const auto& rotation = transform.rotation;
        const auto& scale = transform.scale;
        return json{
            {"translation", json::array({translation.x, translation.y, translation.z})},
            {"rotation", json::array({rotation.w, rotation.x, rotation.y, rotation.z})},
            {"scale", json::array({scale.x, scale.y, scale.z})},
        };
      },
      .deserialize_fn = [](flecs::entity entity, const json& payload) -> void {
        const auto& translation = payload["translation"];
        const auto& rotation = payload["rotation"];
        const auto& scale = payload["scale"];
        entity.set<Transform>({
            .translation = {translation[0].get<float>(), translation[1].get<float>(),
                            translation[2].get<float>()},
            .rotation = {rotation[0].get<float>(), rotation[1].get<float>(),
                         rotation[2].get<float>(), rotation[3].get<float>()},
            .scale = {scale[0].get<float>(), scale[1].get<float>(), scale[2].get<float>()},
        });
      },
  });

  builder.register_component({
      .component_key = "teng.core.camera",
      .has_component_fn = [](flecs::entity entity) { return entity.has<Camera>(); },
      .serialize_fn = [](flecs::entity entity) -> json {
        const auto& camera = entity.get<Camera>();
        return json{
            {"fov_y", camera.fov_y},
            {"z_near", camera.z_near},
            {"z_far", camera.z_far},
            {"primary", camera.primary},
        };
      },
      .deserialize_fn = [](flecs::entity entity, const json& payload) -> void {
        const auto& fov_y = payload["fov_y"];
        const auto& z_near = payload["z_near"];
        const auto& z_far = payload["z_far"];
        const auto& primary = payload["primary"];
        entity.set<Camera>({
            .fov_y = fov_y.get<float>(),
            .z_near = z_near.get<float>(),
            .z_far = z_far.get<float>(),
            .primary = primary.get<bool>(),
        });
      },
  });

  builder.register_component({
      .component_key = "teng.core.directional_light",
      .has_component_fn = [](flecs::entity entity) { return entity.has<DirectionalLight>(); },
      .serialize_fn = [](flecs::entity entity) -> json {
        const auto& light = entity.get<DirectionalLight>();
        return json{
            {"direction", json::array({light.direction.x, light.direction.y, light.direction.z})},
            {"color", json::array({light.color.x, light.color.y, light.color.z})},
            {"intensity", light.intensity},
        };
      },
      .deserialize_fn = [](flecs::entity entity, const json& payload) -> void {
        const auto& direction = payload["direction"];
        const auto& color = payload["color"];
        const auto& intensity = payload["intensity"];
        entity.set<DirectionalLight>({
            .direction = {direction[0].get<float>(), direction[1].get<float>(),
                          direction[2].get<float>()},
            .color = {color[0].get<float>(), color[1].get<float>(), color[2].get<float>()},
            .intensity = intensity.get<float>(),
        });
      },
  });

  builder.register_component({
      .component_key = "teng.core.mesh_renderable",
      .has_component_fn = [](flecs::entity entity) { return entity.has<MeshRenderable>(); },
      .serialize_fn = [](flecs::entity entity) -> json {
        const auto& mesh = entity.get<MeshRenderable>();
        return json{
            {"model", mesh.model.to_string()},
        };
      },
      .deserialize_fn = [](flecs::entity entity, const json& payload) -> void {
        const auto& model = payload["model"];
        entity.set<MeshRenderable>({
            .model = AssetId::parse(model.get<std::string>()).value(),
        });
      },
  });

  builder.register_component({
      .component_key = "teng.core.sprite_renderable",
      .has_component_fn = [](flecs::entity entity) { return entity.has<SpriteRenderable>(); },
      .serialize_fn = [](flecs::entity entity) -> json {
        const auto& sprite = entity.get<SpriteRenderable>();
        return json{
            {"texture", sprite.texture.to_string()},
            {"tint", json::array({sprite.tint.x, sprite.tint.y, sprite.tint.z, sprite.tint.w})},
            {"sorting_layer", sprite.sorting_layer},
            {"sorting_order", sprite.sorting_order},
        };
      },
      .deserialize_fn = [](flecs::entity entity, const json& payload) -> void {
        const auto& texture = payload["texture"];
        const auto& tint = payload["tint"];
        const auto& sorting_layer = payload["sorting_layer"];
        const auto& sorting_order = payload["sorting_order"];
        entity.set<SpriteRenderable>({
            .texture = AssetId::parse(texture.get<std::string>()).value(),
            .tint = {tint[0].get<float>(), tint[1].get<float>(), tint[2].get<float>(),
                     tint[3].get<float>()},
            .sorting_layer = sorting_layer.get<int>(),
            .sorting_order = sorting_order.get<int>(),
        });
      },
  });
}

}  // namespace teng::engine