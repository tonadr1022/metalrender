#pragma once

#include <filesystem>

#include "gfx/rhi/GFXTypes.hpp"

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

namespace rhi {}  // namespace rhi

class PipelineManager {
 public:
  void shutdown();
};

} // namespace TENG_NAMESPACE
