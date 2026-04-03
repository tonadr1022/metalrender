#pragma once

#include "core/Config.hpp"
#include "gfx/rhi/Sampler.hpp"
#include "vulkan/vulkan_core.h"

namespace TENG_NAMESPACE {

namespace gfx::vk {

class VulkanSampler : public rhi::Sampler {
 public:
  explicit VulkanSampler(const rhi::SamplerDesc& desc, VkSampler sampler,
                         uint32_t bindless_idx = rhi::k_invalid_bindless_idx)
      : rhi::Sampler(desc, bindless_idx), sampler_(sampler) {}
  VulkanSampler() = default;
  ~VulkanSampler() = default;

  [[nodiscard]] uint32_t raw_bindless_idx() const { return bindless_idx_; }

  VkSampler sampler_;
};

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
