#pragma once

#include "gfx/GFXTypes.hpp"

namespace gfx {

uint32_t get_block_width_bytes(rhi::TextureFormat format);
uint32_t get_bytes_per_block(rhi::TextureFormat format);

}  // namespace gfx
