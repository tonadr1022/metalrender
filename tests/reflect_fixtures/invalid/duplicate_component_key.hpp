#pragma once

#include "engine/scene/ComponentReflectionMacros.hpp"

namespace teng::reflect_fixtures::invalid {

struct A {
  float x{0.f};
};

struct B {
  float y{0.f};
};

TENG_REFLECT_COMPONENT_BEGIN(A, "teng.fixture.invalid.dup_component_key")
  TENG_REFLECT_MODULE("teng.fixture.invalid", 1)
  TENG_REFLECT_SCHEMA_VERSION(1)
  TENG_REFLECT_STORAGE(Authored)
  TENG_REFLECT_VISIBILITY(Editable)
  TENG_REFLECT_ADD_ON_CREATE(false)
  TENG_REFLECT_FIELD(x, F32, DefaultF32(0.f), ScriptNone)
TENG_REFLECT_COMPONENT_END()

TENG_REFLECT_COMPONENT_BEGIN(B, "teng.fixture.invalid.dup_component_key")
  TENG_REFLECT_MODULE("teng.fixture.invalid", 1)
  TENG_REFLECT_SCHEMA_VERSION(1)
  TENG_REFLECT_STORAGE(Authored)
  TENG_REFLECT_VISIBILITY(Editable)
  TENG_REFLECT_ADD_ON_CREATE(false)
  TENG_REFLECT_FIELD(y, F32, DefaultF32(0.f), ScriptNone)
TENG_REFLECT_COMPONENT_END()

}  // namespace teng::reflect_fixtures::invalid

