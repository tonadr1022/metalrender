#pragma once

#include "core/Result.hpp"
#include "nlohmann/json_fwd.hpp"

namespace teng {

namespace engine::scene {
class ComponentRegistry;
}

namespace engine {

[[nodiscard]] Result<nlohmann::json> serialize_component_schema_to_json(
    const scene::ComponentRegistry& registry);

}  // namespace engine

}  // namespace teng