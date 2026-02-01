#pragma once

#pragma once

#include <cstddef>

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

namespace gfx::vk {

void print_vk_error(size_t x, bool exit_prog = false);

}  // namespace gfx::vk

#ifndef NDEBUG
#define VK_CHECK(x)                     \
  do {                                  \
    ::gfx::vk::print_vk_error(x, true); \
  } while (0)
#else
#define VK_CHECK(x)                      \
  do {                                   \
    ::gfx::vk::print_vk_error(x, false); \
  } while (0)
#endif

}  // namespace TENG_NAMESPACE
