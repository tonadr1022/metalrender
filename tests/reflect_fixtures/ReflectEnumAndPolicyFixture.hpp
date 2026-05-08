#pragma once

#include <cstdint>

#include "engine/scene/ComponentReflectionMacros.hpp"

namespace teng::reflect_fixtures {

enum class TestMode : uint8_t {
  Alpha = 0,
  Beta = 1,
};

struct EnumAndPolicyFixtureComponent {
  TestMode mode{TestMode::Alpha};
  float runtime_value{0.f};
};

TENG_REFLECT_COMPONENT_BEGIN(EnumAndPolicyFixtureComponent, "teng.fixture.enum_and_policy")
  TENG_REFLECT_MODULE("teng.fixture", 1)
  TENG_REFLECT_SCHEMA_VERSION(1)
  TENG_REFLECT_STORAGE(RuntimeSession)
  TENG_REFLECT_VISIBILITY(Hidden)
  TENG_REFLECT_ADD_ON_CREATE(false)

  TENG_REFLECT_ENUM_FIELD(mode, "mode", "teng.fixture.enum_and_policy_mode",
                          DefaultEnum(TestMode::Alpha, "alpha"),
                          ScriptReadWrite,
                          TENG_ENUM_VALUE(TestMode::Alpha, "alpha", 0),
                          TENG_ENUM_VALUE(TestMode::Beta, "beta", 1))

  TENG_REFLECT_FIELD(runtime_value, F32, DefaultF32(0.f), ScriptNone)
TENG_REFLECT_COMPONENT_END()

}  // namespace teng::reflect_fixtures

