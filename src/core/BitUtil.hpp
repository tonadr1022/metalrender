#pragma once

#include <cstddef>

namespace util {

constexpr size_t align_256(size_t n) { return (n + 255) & ~size_t(255); }

}  // namespace util
