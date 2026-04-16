#include "Texture.hpp"

#include "core/Config.hpp"
#include "gfx/rhi/Device.hpp"

namespace TENG_NAMESPACE {

namespace gfx::rhi {

uint32_t get_block_width_bytes(rhi::TextureFormat format) {
  switch (format) {
    case rhi::TextureFormat::ASTC4x4SrgbBlock:
    case rhi::TextureFormat::ASTC4x4UnormBlock:
    case rhi::TextureFormat::Bc7SrgbBlock:
    case rhi::TextureFormat::Bc7UnormBlock:
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
    case rhi::TextureFormat::Bc7SrgbBlock:
    case rhi::TextureFormat::Bc7UnormBlock:
      return 16;
    default:
      ASSERT(0);
      return 1;
  }
}

bool is_depth_format(rhi::TextureFormat format) {
  switch (format) {
    case rhi::TextureFormat::D32float:
      return true;
    default:
      return false;
  }
}

bool is_stencil_format(rhi::TextureFormat format) {
  switch (format) {
    default:
      return false;
  }
}

TexAndViewHolder::~TexAndViewHolder() {
  for (auto v : views) {
    context->destroy(handle, v);
  }
  views.clear();
}

}  // namespace gfx::rhi

}  // namespace TENG_NAMESPACE
