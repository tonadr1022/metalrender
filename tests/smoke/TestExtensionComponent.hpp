#pragma once

#include <cstdint>
#include <string_view>

#include "engine/scene/ComponentReflectionMacros.hpp"
#include "engine/scene/ComponentRegistry.hpp"
#include "engine/scene/SceneComponentContext.hpp"
#include "engine/scene/SceneIds.hpp"
#include "engine/scene/SceneSerializationContext.hpp"

namespace teng::engine {

enum class TestExtensionKind : uint8_t {
  Alpha TENG_ENUM_VALUE(key = "alpha", value = 0) = 0,
  Beta TENG_ENUM_VALUE(key = "beta", value = 1) = 1,
};

/// Test-only ECS component
struct TENG_COMPONENT(key = "teng.test.extension_proof", module = "teng.test", schema_version = 1,
                      storage = "Authored", visibility = "Editable") TestExtensionComponent {
  TENG_FIELD(script = "ReadWrite")
  float health{100.f};

  TENG_FIELD(script = "ReadWrite")
  bool active{true};

  TENG_FIELD(key = "kind", enum_key = "teng.test.extension_proof_kind", script = "ReadWrite")
  TestExtensionKind kind{TestExtensionKind::Alpha};

  TENG_FIELD(asset_kind = "texture", script = "ReadWrite")
  AssetId attachment{};
};

inline constexpr std::string_view k_test_extension_module_id = "teng.test";
inline constexpr std::string_view k_test_extension_component_key = "teng.test.extension_proof";

void register_test_extension_components(scene::ComponentRegistryBuilder& builder);
void register_flecs_test_extension_components(FlecsComponentContextBuilder& builder);
void register_test_extension_serialization(SceneSerializationContextBuilder& builder);

}  // namespace teng::engine
