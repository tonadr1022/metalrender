#pragma once

#include <cstddef>

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

namespace util {

constexpr size_t align_256(size_t n) { return (n + 255) & ~size_t(255); }

// using this to avoid templates everywhere
// NOLINTBEGIN (for bugprone-macro-parentheses)
#define AUGMENT_ENUM_CLASS(classname)                                         \
  constexpr classname operator|(classname lhs, classname rhs) {               \
    using T = std::underlying_type_t<classname>;                              \
    return static_cast<classname>(static_cast<T>(lhs) | static_cast<T>(rhs)); \
  }                                                                           \
  constexpr classname operator&(classname lhs, classname rhs) {               \
    using T = std::underlying_type_t<classname>;                              \
    return static_cast<classname>(static_cast<T>(lhs) & static_cast<T>(rhs)); \
  }                                                                           \
  constexpr classname operator^(classname lhs, classname rhs) {               \
    using T = std::underlying_type_t<classname>;                              \
    return static_cast<classname>(static_cast<T>(lhs) ^ static_cast<T>(rhs)); \
  }                                                                           \
  constexpr classname operator~(classname rhs) {                              \
    using T = std::underlying_type_t<classname>;                              \
    return static_cast<classname>(~static_cast<T>(rhs));                      \
  }                                                                           \
  constexpr classname& operator|=(classname& lhs, classname rhs) {            \
    lhs = lhs | rhs;                                                          \
    return lhs;                                                               \
  }                                                                           \
  constexpr classname& operator&=(classname& lhs, classname rhs) {            \
    lhs = lhs & rhs;                                                          \
    return lhs;                                                               \
  }                                                                           \
  constexpr classname& operator^=(classname& lhs, classname rhs) {            \
    lhs = lhs ^ rhs;                                                          \
    return lhs;                                                               \
  }                                                                           \
  [[nodiscard]] constexpr bool has_flag(classname value, classname flag) {    \
    using T = std::underlying_type_t<classname>;                              \
    return (static_cast<T>(value & flag) != 0ull);                            \
  }

// NOLINTEND

}  // namespace util

}  // namespace TENG_NAMESPACE
