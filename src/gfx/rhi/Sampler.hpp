#pragma once

#include "GFXTypes.hpp"
#include "core/Config.hpp"

namespace TENG_NAMESPACE {

namespace rhi {

struct SamplerDesc {
  FilterMode min_filter{FilterMode::Nearest};
  FilterMode mag_filter{FilterMode::Nearest};
  FilterMode mipmap_mode{FilterMode::Nearest};
  float min_lod{0.f};
  float max_lod{1000.f};
  AddressMode address_mode{AddressMode::Repeat};
  BorderColor border_color{BorderColor::FloatTransparentBlack};
  bool anisotropy_enable{};
  float max_anisotropy{};
  bool compare_enable{};
  CompareOp compare_op{};
  bool bindless{true};
};

class Sampler {
 public:
  Sampler() = default;
  ~Sampler() = default;

  [[nodiscard]] const SamplerDesc& desc() const { return desc_; }
  [[nodiscard]] uint32_t bindless_idx() const { return bindless_idx_; }

 protected:
  explicit Sampler(const SamplerDesc& desc, uint32_t bindless_idx = k_invalid_bindless_idx)
      : desc_(desc), bindless_idx_(bindless_idx) {}

  SamplerDesc desc_;
  uint32_t bindless_idx_{k_invalid_bindless_idx};
};

}  // namespace rhi

}  // namespace TENG_NAMESPACE
