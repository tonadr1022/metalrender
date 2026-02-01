#pragma once

#include <cstdio>
#include <cstdlib>

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

// Credit: ChatGPT 5.0 (I was too lazy)

#define ALL_ASSERTS_ENABLED 1

#ifndef NDEBUG
#ifndef ALL_ASSERTS_ENABLED
#define ALL_ASSERTS_ENABLED 1
#endif
#endif

class AlwaysAssert {
 public:
  static void fail(const char* expr, const char* file, int line) {
    std::fprintf(stderr, "Assertion failed: (%s), file %s, line %d\n", expr, file, line);
    std::abort();
  }

  static void fail(const char* expr, const char* file, int line, const char* msg) {
    std::fprintf(stderr, "Assertion failed: (%s), file %s, line %d: %s\n", expr, file, line, msg);
    std::abort();
  }
};

#define ALWAYS_ASSERT(expr, ...)                                               \
  do {                                                                         \
    if (!(expr)) {                                                             \
      AlwaysAssert::fail(#expr, __FILE__, __LINE__ __VA_OPT__(, __VA_ARGS__)); \
    }                                                                          \
  } while (0)

#ifdef ALL_ASSERTS_ENABLED
#define ASSERT(expr, ...)                                                      \
  do {                                                                         \
    if (!(expr)) {                                                             \
      AlwaysAssert::fail(#expr, __FILE__, __LINE__ __VA_OPT__(, __VA_ARGS__)); \
    }                                                                          \
  } while (0)
#else
#define ASSERT(expr, ...)
#endif

} // namespace TENG_NAMESPACE
