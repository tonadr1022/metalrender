#pragma once

#include "engine/scene/SceneComponentContext.hpp"

namespace TENG_NAMESPACE::engine {

void register_core_components(core::ComponentRegistryBuilder& builder);
void register_flecs_core_components(SceneComponentContextBuilder& builder);

}  // namespace TENG_NAMESPACE::engine
