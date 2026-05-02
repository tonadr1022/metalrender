#pragma once

#include <flecs.h>

#include <string>
#include <vector>

#include "core/ComponentRegistry.hpp"
#include "core/Diagnostic.hpp"

namespace TENG_NAMESPACE::engine {

using ApplyOnCreateFn = void (*)(flecs::entity);
using RegisterFlecsFn = void (*)(flecs::world&);

struct FlecsComponentBinding {
  RegisterFlecsFn register_flecs_fn{};
  ApplyOnCreateFn apply_on_create_fn{};
};

struct SceneComponentContext {
  core::ComponentRegistry registry;
  std::vector<ApplyOnCreateFn> apply_on_create_fns;
  std::vector<RegisterFlecsFn> flecs_register_fns;
};

class SceneComponentContextBuilder {
 public:
  void register_component(core::ComponentRegistration component,
                          FlecsComponentBinding flecs_component_binding);
  [[nodiscard]] core::ComponentRegistryBuilder& registry_builder() { return registry_builder_; }

  /// On failure, clears `out` and appends diagnostics to `report`.
  [[nodiscard]] bool try_freeze(SceneComponentContext& out, core::DiagnosticReport& report) const;

 private:
  core::ComponentRegistryBuilder registry_builder_;

  struct FlecsComponentRegisterInfo {
    FlecsComponentBinding binding;
    std::string component_key;
  };

  std::vector<FlecsComponentRegisterInfo> flecs_component_register_infos_;
};

}  // namespace TENG_NAMESPACE::engine
