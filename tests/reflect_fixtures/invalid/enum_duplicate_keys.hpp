#pragma once

#include <cstdint>

#include "engine/scene/ComponentReflectionMacros.hpp"

namespace teng::reflect_fixtures::invalid {

enum class BadEnum : uint8_t {
  A TENG_ENUM_VALUE(key = "alpha", value = 0) = 0,
  B TENG_ENUM_VALUE(key = "alpha", value = 1) = 1,
};

struct TENG_COMPONENT(key = "teng.fixture.invalid.enum_dup_keys", module = "teng.fixture.invalid",
                      schema_version = 1, storage = "Authored", visibility = "Editable") D {
  TENG_FIELD(key = "mode", enum_key = "teng.fixture.invalid.enum_dup_keys_mode", script = "None")
  BadEnum mode{BadEnum::A};
};

}  // namespace teng::reflect_fixtures::invalid
