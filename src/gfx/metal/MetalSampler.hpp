#pragma once

#include "gfx/rhi/Sampler.hpp"

namespace MTL {

class SamplerState;

}

class MetalSampler : public rhi::Sampler {
 public:
  MetalSampler(const rhi::SamplerDesc& desc, MTL::SamplerState* sampler,
               uint32_t bindless_idx = rhi::k_invalid_bindless_idx)
      : rhi::Sampler(desc, bindless_idx), sampler_(sampler) {}
  MetalSampler() = default;
  ~MetalSampler() = default;
  [[nodiscard]] MTL::SamplerState* sampler() { return sampler_; }

 private:
  MTL::SamplerState* sampler_{};
};
