#pragma once

#include <print>

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

#define LINFO(...) std::println("" __VA_ARGS__)
// #define LINFO(...) std::println("[info] " __VA_ARGS__)
#define LWARN(...) std::println("[warn]     " __VA_ARGS__)
#define LERROR(...) std::println("[error]    " __VA_ARGS__)
#define LCRITICAL(...) std::println("[critical] " __VA_ARGS__)
#define LDEBUG(...) std::println("[debug]   " __VA_ARGS__)

} // namespace TENG_NAMESPACE
