#pragma once

#include "engine/scene/ComponentReflectionMacros.hpp"

namespace teng::reflect_fixtures::invalid {

struct C {
  float health{0.f};
  float health2{0.f};
};

TENG_REFLECT_COMPONENT_BEGIN(C, "teng.fixture.invalid.dup_field_key")
  TENG_REFLECT_MODULE("teng.fixture.invalid", 1)
  TENG_REFLECT_SCHEMA_VERSION(1)
  TENG_REFLECT_STORAGE(Authored)
  TENG_REFLECT_VISIBILITY(Editable)
  TENG_REFLECT_ADD_ON_CREATE(false)

  // Key defaults to member name for scalar fields; duplicate keys should fail.
  TENG_REFLECT_FIELD(health, F32, DefaultF32(0.f), ScriptNone)
  TENG_REFLECT_FIELD(health, F32, DefaultF32(0.f), ScriptNone)
TENG_REFLECT_COMPONENT_END()

}  // namespace teng::reflect_fixtures::invalid

