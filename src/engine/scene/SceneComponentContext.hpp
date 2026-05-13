#pragma once

#include <vector>

#include "engine/scene/ComponentRegistry.hpp"

namespace TENG_NAMESPACE::engine {

using ApplyOnCreateFn = scene::ApplyOnCreateFn;
using RegisterFlecsFn = scene::RegisterFlecsFn;

struct FlecsComponentContext {
  std::vector<ApplyOnCreateFn> apply_on_create_fns;
  std::vector<RegisterFlecsFn> flecs_register_fns;
};

[[nodiscard]] bool make_flecs_component_context(const scene::ComponentRegistry& registry,
                                                FlecsComponentContext& out,
                                                core::DiagnosticReport& report);

}  // namespace TENG_NAMESPACE::engine
