#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "engine/scene/ComponentReflectionMacros.hpp"
#include "engine/scene/ComponentRegistry.hpp"
#include "engine/scene/SceneIds.hpp"

namespace teng::engine {

enum class TestExtensionKind : uint8_t {
  Alpha TENG_ENUM_N(0),
  Beta TENG_ENUM_N(1),
};

/// Test-only ECS component
struct TENG_COMPONENT(key = "teng.test.extension_proof", module = "teng.test", schema_version = 1,
                      storage = "Authored", visibility = "Editable") TestExtensionComponent {
  TENG_FIELD(script = "ReadWrite")
  float health{100.f};

  TENG_FIELD(script = "ReadWrite")
  bool active{true};

  TENG_FIELD(key = "kind", script = "ReadWrite")
  TestExtensionKind kind{TestExtensionKind::Alpha};

  TENG_FIELD(asset_kind = "texture", script = "ReadWrite")
  AssetId attachment{};
};

inline constexpr std::string_view k_test_extension_module_id = "teng.test";
inline constexpr std::string_view k_test_extension_component_key = "teng.test.extension_proof";

[[nodiscard]] std::span<const scene::ComponentModuleDescriptor> test_extension_component_modules();

}  // namespace teng::engine
