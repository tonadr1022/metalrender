#pragma once

#include "core/Config.hpp"
#include "gfx/rhi/Pipeline.hpp"
#include "vulkan/vulkan_core.h"

namespace TENG_NAMESPACE {

namespace gfx::vk {

class VulkanPipeline : public rhi::Pipeline {
 public:
  VulkanPipeline() = default;
  VulkanPipeline(const rhi::GraphicsPipelineCreateInfo& ginfo, VkPipeline pipeline)
      : rhi::Pipeline(ginfo), pipeline_(pipeline) {}
  VulkanPipeline(const rhi::ShaderCreateInfo& cinfo, VkPipeline pipeline)
      : rhi::Pipeline(cinfo), pipeline_(pipeline) {}

  VkPipeline pipeline_{};
};

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
