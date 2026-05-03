#pragma once

#include <cstdlib>
#include <format>
#include <print>
#include <utility>

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
    std::println(stderr, "Assertion failed: ({}), file {}, line {}", expr, file, line);
    std::abort();
  }

  static void fail(const char* expr, const char* file, int line, const char* msg) {
    std::println(stderr, "Assertion failed: ({}), file {}, line {}: {}", expr, file, line, msg);
    std::abort();
  }

  template <typename... Args>
  static void fail(const char* expr,
                   const char* file,
                   int line,
                   std::format_string<Args...> fmt,
                   Args&&... args) {
    std::print(stderr, "Assertion failed: ({}), file {}, line {}: ", expr, file, line);
    std::println(stderr, fmt, std::forward<Args>(args)...);
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

}  // namespace TENG_NAMESPACE
