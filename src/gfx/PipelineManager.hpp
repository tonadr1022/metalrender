#pragma once

#include <filesystem>

#include "core/Config.hpp"
#include "gfx/rhi/GFXTypes.hpp"

namespace TENG_NAMESPACE {

namespace rhi {}  // namespace rhi

class PipelineManager {
 public:
  void shutdown();
};

}  // namespace TENG_NAMESPACE
