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
  Alpha = 0,
  Beta = 1,
};

/// Test-only ECS component
struct TestExtensionComponent {
  float health{100.f};
  bool active{true};
  TestExtensionKind kind{TestExtensionKind::Alpha};
  AssetId attachment{};
};

inline constexpr std::string_view k_test_extension_module_id = "teng.test";
inline constexpr std::string_view k_test_extension_component_key = "teng.test.extension_proof";

TENG_REFLECT_COMPONENT_BEGIN(TestExtensionComponent, "teng.test.extension_proof")
  TENG_REFLECT_MODULE("teng.test", 1)
  TENG_REFLECT_SCHEMA_VERSION(1)
  TENG_REFLECT_STORAGE(Authored)
  TENG_REFLECT_VISIBILITY(Editable)
  TENG_REFLECT_ADD_ON_CREATE(false)
  TENG_REFLECT_FIELD(health, F32, DefaultF32(100.f), ScriptReadWrite)
  TENG_REFLECT_FIELD(active, Bool, DefaultBool(true), ScriptReadWrite)
  TENG_REFLECT_ENUM_FIELD(kind, "kind", "teng.test.extension_proof_kind",
                          DefaultEnum(TestExtensionKind::Alpha, "alpha"),
                          ScriptReadWrite,
                          TENG_ENUM_VALUE(TestExtensionKind::Alpha, "alpha", 0),
                          TENG_ENUM_VALUE(TestExtensionKind::Beta, "beta", 1))
  TENG_REFLECT_ASSET_FIELD(attachment, "attachment", "texture", DefaultAssetId(""),
                           ScriptReadWrite)
TENG_REFLECT_COMPONENT_END()

void register_test_extension_components(scene::ComponentRegistryBuilder& builder);
void register_flecs_test_extension_components(FlecsComponentContextBuilder& builder);
void register_test_extension_serialization(SceneSerializationContextBuilder& builder);

}  // namespace teng::engine
