#pragma once

#include <string_view>
#include <vector>

#include "engine/scene/ComponentRegistry.hpp"

namespace teng::engine {

struct SceneSerializationContext;

namespace scene::authoring {

struct InspectorFieldInfo {
  std::string_view key;
  std::string_view member_name;
  ComponentFieldKind kind{};
  ScriptExposure script_exposure{ScriptExposure::None};
};

struct InspectorComponentInfo {
  std::string_view component_key;
  ComponentSchemaVisibility visibility{ComponentSchemaVisibility::Editable};
  std::vector<InspectorFieldInfo> fields;
};

[[nodiscard]] std::vector<InspectorComponentInfo> editable_component_inspector(
    const SceneSerializationContext& serialization);

}  // namespace scene::authoring
}  // namespace teng::engine
