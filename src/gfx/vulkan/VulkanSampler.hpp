#pragma once

#include "gfx/Sampler.hpp"

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

namespace gfx::vk {

class VulkanSampler : public rhi::Sampler {
 public:
  explicit VulkanSampler(const rhi::SamplerDesc& desc,
                         uint32_t bindless_idx = rhi::k_invalid_bindless_idx)
      : rhi::Sampler(desc, bindless_idx) {}
  VulkanSampler() = default;
  ~VulkanSampler() = default;
};

}  // namespace gfx::vk

} // namespace TENG_NAMESPACE
