#pragma once

#include "core/Config.hpp"
#include "gfx/rhi/Sampler.hpp"

namespace MTL {
class SamplerState;
}

namespace TENG_NAMESPACE {

namespace gfx::mtl {

class Sampler : public rhi::Sampler {
 public:
  Sampler(const rhi::SamplerDesc& desc, MTL::SamplerState* sampler,
          uint32_t bindless_idx = rhi::k_invalid_bindless_idx)
      : rhi::Sampler(desc, bindless_idx), sampler_(sampler) {}
  Sampler() = default;
  ~Sampler() = default;
  [[nodiscard]] MTL::SamplerState* sampler() { return sampler_; }

 private:
  MTL::SamplerState* sampler_{};
};

}  // namespace gfx::mtl

}  // namespace TENG_NAMESPACE