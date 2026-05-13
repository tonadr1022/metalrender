#pragma once

#include <span>

#include "engine/scene/ComponentRegistry.hpp"

namespace TENG_NAMESPACE::engine {

[[nodiscard]] std::span<const scene::ComponentModuleDescriptor> core_component_modules();

}  // namespace TENG_NAMESPACE::engine
