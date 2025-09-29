#pragma once
#include "GFXTypes.hpp"

namespace rhi {

class Texture {
 public:
  Texture() = default;
  [[nodiscard]] const TextureDesc& desc() const { return desc_; }
  [[nodiscard]] uint32_t gpu_slot() const { return gpu_slot_; }

  static constexpr uint32_t k_invalid_gpu_slot = UINT32_MAX;

 protected:
  explicit Texture(const TextureDesc& desc, uint32_t gpu_slot = k_invalid_gpu_slot)
      : desc_(desc), gpu_slot_(gpu_slot) {}
  TextureDesc desc_{};
  uint32_t gpu_slot_{k_invalid_gpu_slot};
};

}  // namespace rhi
