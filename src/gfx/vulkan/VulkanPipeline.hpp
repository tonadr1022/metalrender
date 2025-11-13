#pragma once

#include "gfx/Pipeline.hpp"

namespace gfx::vk {

class VulkanPipeline : public rhi::Pipeline {
 public:
  VulkanPipeline() = default;
  ~VulkanPipeline() override = default;
};

}  // namespace gfx::vk
