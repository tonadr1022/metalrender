#pragma once

#include "core/Result.hpp"
#include "nlohmann/json_fwd.hpp"

namespace teng {

namespace core {
class ComponentRegistry;
}

namespace engine {

[[nodiscard]] Result<nlohmann::json> serialize_component_schema_to_json(
    const core::ComponentRegistry& registry);

}  // namespace engine

}  // namespace teng