#pragma once

#include <cstdint>

#include "engine/scene/ComponentReflectionMacros.hpp"

namespace teng::reflect_fixtures {

struct ScalarAndAssetFixtureComponent {
  float health{100.f};
  bool active{true};
  int32_t sorting_layer{0};
  uint32_t mask{0};
  // AssetId is an engine type; for Task 2 fixtures we only need it to be a name.
  struct AssetId {
    uint64_t value{};
  };
  AssetId attachment{};
};

TENG_REFLECT_COMPONENT_BEGIN(ScalarAndAssetFixtureComponent, "teng.fixture.scalar_and_asset")
  TENG_REFLECT_MODULE("teng.fixture", 1)
  TENG_REFLECT_SCHEMA_VERSION(1)
  TENG_REFLECT_STORAGE(Authored)
  TENG_REFLECT_VISIBILITY(Editable)
  TENG_REFLECT_ADD_ON_CREATE(false)

  TENG_REFLECT_FIELD(health, F32, DefaultF32(100.f), ScriptReadWrite)
  TENG_REFLECT_FIELD(active, Bool, DefaultBool(true), ScriptRead)
  TENG_REFLECT_FIELD(sorting_layer, I32, int64_t{0}, ScriptNone)
  TENG_REFLECT_FIELD(mask, U32, uint64_t{0}, ScriptNone)
  TENG_REFLECT_ASSET_FIELD(attachment, "attachment", "texture", DefaultAssetId(""), ScriptReadWrite)
TENG_REFLECT_COMPONENT_END()

}  // namespace teng::reflect_fixtures

