#pragma once

#include <cstdint>
#include <string>

#include "core/Config.hpp"

namespace TENG_NAMESPACE {
namespace rhi {

struct QueryPoolDesc {
  uint32_t count;
  std::string name;
};

class QueryPool {};

}  // namespace rhi

} // namespace TENG_NAMESPACE
