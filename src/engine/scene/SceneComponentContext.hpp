#pragma once

#include <flecs.h>

#include <string_view>
#include <vector>

#include "core/ComponentRegistry.hpp"

namespace TENG_NAMESPACE::engine {

using ApplyOnCreateFn = void (*)(flecs::entity);
using RegisterFlecsFn = void (*)(flecs::world&);

struct FlecsComponentBinding {
  std::string_view component_key;
  RegisterFlecsFn register_flecs_fn{};
  ApplyOnCreateFn apply_on_create_fn{};
};

struct FlecsComponentContext {
  std::vector<ApplyOnCreateFn> apply_on_create_fns;
  std::vector<RegisterFlecsFn> flecs_register_fns;
};

class FlecsComponentContextBuilder {
 public:
  explicit FlecsComponentContextBuilder(const core::ComponentRegistry& registry)
      : registry_(registry) {}

  void register_flecs_component(FlecsComponentBinding flecs_component_binding);
  [[nodiscard]] const core::ComponentRegistry& registry() { return registry_; }

  /// On failure, clears `out` and appends diagnostics to `report`.
  [[nodiscard]] bool try_freeze(FlecsComponentContext& out, core::DiagnosticReport& report) const;

 private:
  const core::ComponentRegistry& registry_;

  std::vector<FlecsComponentBinding> flecs_component_bindings_;
};

}  // namespace TENG_NAMESPACE::engine
