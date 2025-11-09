#pragma once

#include "gfx/Pipeline.hpp"

class VulkanPipeline : public rhi::Pipeline {
 public:
  VulkanPipeline() = default;
  ~VulkanPipeline() override = default;
};
