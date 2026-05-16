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

void TexAndViewHolder::destroy_views() {
  if (!context) {
    views.clear();
    return;
  }
  for (const TextureViewHandle view : views) {
    context->destroy(handle, view);
  }
  views.clear();
}

TexAndViewHolder& TexAndViewHolder::operator=(TexAndViewHolder&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  destroy_views();
  static_cast<TextureHandleHolder&>(*this) = std::move(static_cast<TextureHandleHolder&>(other));
  views = std::move(other.views);
  return *this;
}

TexAndViewHolder::~TexAndViewHolder() { destroy_views(); }

}  // namespace gfx::rhi

}  // namespace TENG_NAMESPACE
