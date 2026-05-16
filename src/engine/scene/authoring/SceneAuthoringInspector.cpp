#include "engine/scene/authoring/SceneAuthoringInspector.hpp"

#include <utility>

#include "engine/scene/SceneSerializationContext.hpp"

namespace teng::engine::scene::authoring {

std::vector<InspectorComponentInfo> editable_component_inspector(
    const SceneSerializationContext& serialization) {
  std::vector<InspectorComponentInfo> out;
  for (const FrozenComponentRecord& component : serialization.component_registry().components()) {
    if (component.storage != ComponentStoragePolicy::Authored ||
        component.visibility != ComponentSchemaVisibility::Editable) {
      continue;
    }

    InspectorComponentInfo info{
        .component_key = component.component_key,
        .visibility = component.visibility,
    };
    info.fields.reserve(component.fields.size());
    for (const FrozenComponentFieldRecord& field : component.fields) {
      info.fields.push_back(InspectorFieldInfo{
          .key = field.key,
          .member_name = field.member_name,
          .kind = field.kind,
          .script_exposure = field.script_exposure,
      });
    }
    out.push_back(std::move(info));
  }
  return out;
}

}  // namespace teng::engine::scene::authoring
