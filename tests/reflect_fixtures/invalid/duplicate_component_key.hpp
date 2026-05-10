#pragma once

#include "engine/scene/ComponentReflectionMacros.hpp"

namespace teng::reflect_fixtures::invalid {

struct TENG_COMPONENT(key = "teng.fixture.invalid.dup_component_key",
                      module = "teng.fixture.invalid", schema_version = 1, storage = "Authored",
                      visibility = "Editable") A {
  TENG_FIELD(script = "None")
  float x{0.f};
};

struct TENG_COMPONENT(key = "teng.fixture.invalid.dup_component_key",
                      module = "teng.fixture.invalid", schema_version = 1, storage = "Authored",
                      visibility = "Editable") B {
  TENG_FIELD(script = "None")
  float y{0.f};
};

}  // namespace teng::reflect_fixtures::invalid
