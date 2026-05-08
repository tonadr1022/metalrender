#pragma once

#include <string>
#include <string_view>

#include "engine/scene/ComponentRegistry.hpp"
#include "engine/scene/SceneComponentContext.hpp"
#include "engine/scene/SceneIds.hpp"
#include "engine/scene/SceneSerializationContext.hpp"

namespace teng::engine {

/// Test-only ECS component
struct TestExtensionComponent {
  float health{100.f};
  bool active{true};
  /// Authored enum key; runtime stores string key matching schema enum values.
  std::string kind{"alpha"};
  AssetId attachment{};
};

inline constexpr std::string_view k_test_extension_module_id = "teng.test";
inline constexpr std::string_view k_test_extension_component_key = "teng.test.extension_proof";

void register_test_extension_components(scene::ComponentRegistryBuilder& builder);
void register_flecs_test_extension_components(FlecsComponentContextBuilder& builder);
void register_test_extension_serialization(SceneSerializationContextBuilder& builder);

}  // namespace teng::engine
