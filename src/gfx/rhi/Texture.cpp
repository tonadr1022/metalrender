#include "Texture.hpp"

namespace gfx {

uint32_t get_block_width_bytes(rhi::TextureFormat format) {
  switch (format) {
    case rhi::TextureFormat::ASTC4x4SrgbBlock:
    case rhi::TextureFormat::ASTC4x4UnormBlock:
      return 4;
    default:
      ASSERT(0);
      return 1;
  }
}

uint32_t get_bytes_per_block(rhi::TextureFormat format) {
  switch (format) {
    case rhi::TextureFormat::ASTC4x4SrgbBlock:
    case rhi::TextureFormat::ASTC4x4UnormBlock:
      return 16;
    default:
      ASSERT(0);
      return 1;
  }
}

}  // namespace gfx
