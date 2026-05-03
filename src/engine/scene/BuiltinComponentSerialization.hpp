#pragma once

namespace teng {
namespace engine {

class SceneSerializationContextBuilder;

void register_builtin_component_serialization(SceneSerializationContextBuilder& builder);

}  // namespace engine
}  // namespace teng