#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>

#include "core/Result.hpp"
#include "engine/scene/SceneIds.hpp"

namespace teng::engine {

class Scene;
struct SceneSerializationContext;

namespace scene {
struct FrozenComponentRecord;
}

namespace scene::authoring {

[[nodiscard]] std::string entity_guid_lower_hex(EntityGuid guid);

[[nodiscard]] nlohmann::ordered_json default_component_payload(
    const FrozenComponentRecord& component);

[[nodiscard]] Result<nlohmann::ordered_json> validated_candidate_scene_json(
    const SceneSerializationContext& serialization, const nlohmann::json& candidate);

[[nodiscard]] Result<nlohmann::json> canonical_component_payload(
    const nlohmann::json& canonical_scene_json, EntityGuid entity, std::string_view component_key);

[[nodiscard]] Result<nlohmann::ordered_json> candidate_with_created_entity(
    const Scene& scene, const SceneSerializationContext& serialization, EntityGuid entity,
    std::string_view name);
[[nodiscard]] Result<nlohmann::ordered_json> candidate_with_renamed_entity(
    const Scene& scene, const SceneSerializationContext& serialization, EntityGuid entity,
    std::string_view name);
[[nodiscard]] Result<nlohmann::ordered_json> candidate_without_entity(
    const Scene& scene, const SceneSerializationContext& serialization, EntityGuid entity);
[[nodiscard]] Result<nlohmann::ordered_json> candidate_with_component_payload(
    const Scene& scene, const SceneSerializationContext& serialization, EntityGuid entity,
    std::string_view component_key, const nlohmann::json& payload);
[[nodiscard]] Result<nlohmann::ordered_json> candidate_without_component(
    const Scene& scene, const SceneSerializationContext& serialization, EntityGuid entity,
    std::string_view component_key);
[[nodiscard]] Result<nlohmann::ordered_json> candidate_with_component_field(
    const Scene& scene, const SceneSerializationContext& serialization, EntityGuid entity,
    std::string_view component_key, std::string_view field_key, const nlohmann::json& value);

}  // namespace scene::authoring
}  // namespace teng::engine
