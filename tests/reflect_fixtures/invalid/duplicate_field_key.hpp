#pragma once

#include "engine/scene/ComponentReflectionMacros.hpp"

namespace teng::reflect_fixtures::invalid {

struct TENG_COMPONENT(key = "teng.fixture.invalid.dup_field_key", module = "teng.fixture.invalid",
                      schema_version = 1, storage = "Authored", visibility = "Editable") C {
  TENG_FIELD(key = "health", script = "None")
  float health{0.f};

  TENG_FIELD(key = "health", script = "None")
  float health2{0.f};
};

}  // namespace teng::reflect_fixtures::invalid
