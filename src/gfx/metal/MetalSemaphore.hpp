#pragma once

#include <Metal/Metal.hpp>

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

struct MetalSemaphore {
  NS::SharedPtr<MTL::Event> event;
  size_t value;
};

}  // namespace TENG_NAMESPACE
