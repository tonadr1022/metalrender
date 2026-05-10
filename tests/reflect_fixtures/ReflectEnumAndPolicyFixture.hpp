#pragma once

#include <cstdint>

#include "engine/scene/ComponentReflectionMacros.hpp"

namespace teng::reflect_fixtures {

enum class TestMode : uint8_t {
  Alpha TENG_ENUM_VALUE(key = "alpha", value = 0) = 0,
  Beta TENG_ENUM_VALUE(key = "beta", value = 1) = 1,
};

struct TENG_COMPONENT(key = "teng.fixture.enum_and_policy", module = "teng.fixture",
                      schema_version = 1, storage = "RuntimeSession", visibility = "Hidden")
    EnumAndPolicyFixtureComponent {
  TENG_FIELD(key = "mode", enum_key = "teng.fixture.enum_and_policy_mode", script = "ReadWrite")
  TestMode mode{TestMode::Alpha};

  TENG_FIELD(script = "None")
  float runtime_value{0.f};
};

}  // namespace teng::reflect_fixtures
