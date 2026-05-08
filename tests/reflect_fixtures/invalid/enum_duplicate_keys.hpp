#pragma once

#include <cstdint>

#include "engine/scene/ComponentReflectionMacros.hpp"

namespace teng::reflect_fixtures::invalid {

enum class BadEnum : uint8_t { A = 0, B = 1 };

struct D {
  BadEnum mode{BadEnum::A};
};

TENG_REFLECT_COMPONENT_BEGIN(D, "teng.fixture.invalid.enum_dup_keys")
  TENG_REFLECT_MODULE("teng.fixture.invalid", 1)
  TENG_REFLECT_SCHEMA_VERSION(1)
  TENG_REFLECT_STORAGE(Authored)
  TENG_REFLECT_VISIBILITY(Editable)
  TENG_REFLECT_ADD_ON_CREATE(false)

  TENG_REFLECT_ENUM_FIELD(mode, "mode", "teng.fixture.invalid.enum_dup_keys_mode",
                          DefaultEnum(BadEnum::A, "alpha"),
                          ScriptNone,
                          TENG_ENUM_VALUE(BadEnum::A, "alpha", 0),
                          TENG_ENUM_VALUE(BadEnum::B, "alpha", 1))
TENG_REFLECT_COMPONENT_END()

}  // namespace teng::reflect_fixtures::invalid

