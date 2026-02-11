#pragma once

#include "GFXTypes.hpp"
#include "core/Config.hpp"

namespace TENG_NAMESPACE {

namespace gfx {

uint32_t get_block_width_bytes(rhi::TextureFormat format);
uint32_t get_bytes_per_block(rhi::TextureFormat format);
bool is_depth_format(rhi::TextureFormat format);
bool is_stencil_format(rhi::TextureFormat format);

}  // namespace gfx

namespace rhi {

class Texture {
 public:
  Texture() = default;
  [[nodiscard]] const TextureDesc& desc() const { return desc_; }
  [[nodiscard]] uint32_t bindless_idx() const {
    ASSERT(bindless_idx_ != k_invalid_bindless_idx);
    return bindless_idx_;
  }

 protected:
  explicit Texture(const TextureDesc& desc, uint32_t gpu_slot = k_invalid_bindless_idx)
      : desc_(desc), bindless_idx_(gpu_slot) {}
  TextureDesc desc_{};
  uint32_t bindless_idx_{k_invalid_bindless_idx};
};

}  // namespace rhi

}  // namespace TENG_NAMESPACE
