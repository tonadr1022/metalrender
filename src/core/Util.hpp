#pragma once

#include <cstdint>
#include <string>

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

#define ARRAY_SIZE(arr) (sizeof((arr)) / sizeof((arr)[0]))

std::string binary_rep(size_t val);

constexpr uint64_t align_up(uint64_t n, uint64_t alignment) {
  return (n + alignment - 1) & ~(alignment - 1);
}

constexpr size_t align_divide_up(size_t n, size_t alignment) {
  return (n + alignment - 1) / alignment;
}

}  // namespace TENG_NAMESPACE
