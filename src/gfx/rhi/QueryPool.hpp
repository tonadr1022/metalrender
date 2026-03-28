#pragma once

#include <cstdint>
#include <string>

#include "core/Config.hpp"

namespace TENG_NAMESPACE {
namespace gfx::rhi {

struct QueryPoolDesc {
  uint32_t count;
  std::string name;
};

class QueryPool {};

}  // namespace gfx::rhi

}  // namespace TENG_NAMESPACE
