#pragma once

#include "engine/scene/SceneComponentContext.hpp"

namespace TENG_NAMESPACE::engine {

void register_core_components(scene::ComponentRegistryBuilder& builder);
void register_flecs_core_components(FlecsComponentContextBuilder& builder);

}  // namespace TENG_NAMESPACE::engine
