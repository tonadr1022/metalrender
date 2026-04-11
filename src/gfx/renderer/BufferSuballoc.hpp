#pragma once

#include "core/Config.hpp"
#include "gfx/rhi/GFXTypes.hpp"

namespace TENG_NAMESPACE {

namespace gfx {

struct BufferSuballoc {
  rhi::BufferHandle buf;
  uint32_t bindless_idx;
  uint32_t offset_bytes;
  void* write_ptr;
};

}  // namespace gfx

}  // namespace TENG_NAMESPACE