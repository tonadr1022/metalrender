#pragma once

#include "GFXTypes.hpp"

namespace rhi {

class Texture {
 public:
  Texture() = default;
  [[nodiscard]] const TextureDesc& desc() const { return desc_; }
  [[nodiscard]] uint32_t bindless_idx() const { return bindless_idx_; }

 protected:
  explicit Texture(const TextureDesc& desc, uint32_t gpu_slot = k_invalid_bindless_idx)
      : desc_(desc), bindless_idx_(gpu_slot) {}
  TextureDesc desc_{};
  uint32_t bindless_idx_{k_invalid_bindless_idx};
};

}  // namespace rhi
