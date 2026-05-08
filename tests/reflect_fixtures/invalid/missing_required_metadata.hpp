#pragma once

#include "engine/scene/ComponentReflectionMacros.hpp"

namespace teng::reflect_fixtures::invalid {

struct E {
  float x{0.f};
};

TENG_REFLECT_COMPONENT_BEGIN(E, "teng.fixture.invalid.missing_metadata")
  // Missing: MODULE / SCHEMA_VERSION / STORAGE / VISIBILITY / ADD_ON_CREATE
  TENG_REFLECT_FIELD(x, F32, DefaultF32(0.f), ScriptNone)
TENG_REFLECT_COMPONENT_END()

}  // namespace teng::reflect_fixtures::invalid

