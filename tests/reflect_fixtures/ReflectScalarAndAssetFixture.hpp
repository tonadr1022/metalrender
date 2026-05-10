#pragma once

#include <cstdint>

#include "engine/scene/ComponentReflectionMacros.hpp"
#include "engine/scene/SceneIds.hpp"

namespace teng::reflect_fixtures {

struct TENG_COMPONENT(key = "teng.fixture.scalar_and_asset", module = "teng.fixture",
                      schema_version = 1, storage = "Authored", visibility = "Editable")
    ScalarAndAssetFixtureComponent {
  TENG_FIELD(script = "ReadWrite")
  float health{100.f};

  TENG_FIELD(script = "Read")
  bool active{true};

  TENG_FIELD(script = "None")
  int32_t sorting_layer{0};

  TENG_FIELD(script = "None")
  uint32_t mask{0};

  TENG_FIELD(asset_kind = "texture", script = "ReadWrite")
  teng::engine::AssetId attachment{};
};

}  // namespace teng::reflect_fixtures
