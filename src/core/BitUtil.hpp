#pragma once

#include <cstddef>

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

namespace util {

constexpr size_t align_256(size_t n) { return (n + 255) & ~size_t(255); }

}  // namespace util

}  // namespace TENG_NAMESPACE
