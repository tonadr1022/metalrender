#pragma once

#include "engine/scene/ComponentReflectionMacros.hpp"

namespace teng::reflect_fixtures::invalid {

struct TENG_COMPONENT(key = "teng.fixture.invalid.missing_metadata") E {
  TENG_FIELD(script = "None")
  float x{0.f};
};

}  // namespace teng::reflect_fixtures::invalid
