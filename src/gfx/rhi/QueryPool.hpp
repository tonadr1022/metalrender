#pragma once

#include <cstdint>
#include <string>
namespace rhi {

struct QueryPoolDesc {
  uint32_t count;
  std::string name;
};

class QueryPool {};

}  // namespace rhi
