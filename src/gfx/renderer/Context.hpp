#pragma once

#include "gfx/renderer/RendererSettings.hpp"

namespace TENG_NAMESPACE {

namespace gfx {

// Non-thread-safe singleton of non-owning global state
struct Context {
  RendererSettings settings;
};

inline Context& get_ctx() {
  static Context ctx;
  return ctx;
}

}  // namespace gfx
}  // namespace TENG_NAMESPACE