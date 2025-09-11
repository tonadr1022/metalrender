#include "MetalUtil.hpp"

#include <Metal/Metal.hpp>
#include <cassert>

#include "core/Logger.hpp"

namespace util::mtl {

void print_err(NS::Error* err) {
  assert(err);
  LINFO("{}", err->localizedDescription()->cString(NS::ASCIIStringEncoding));
}

}  // namespace util::mtl
