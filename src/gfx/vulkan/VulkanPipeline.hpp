#pragma once

#include "core/Config.hpp"
#include "gfx/rhi/Pipeline.hpp"
#include "vulkan/vulkan_core.h"

namespace TENG_NAMESPACE {

namespace gfx::vk {

class VulkanPipeline : public rhi::Pipeline {
 public:
  VulkanPipeline() = default;
  VulkanPipeline(const rhi::GraphicsPipelineCreateInfo& ginfo, VkPipeline pipeline,
                 VkPipelineLayout layout, VkDescriptorSetLayout descriptor_set_layout,
                 std::vector<VkDescriptorSetLayoutBinding>&& layout_bindings)
      : rhi::Pipeline(ginfo),
        pipeline_(pipeline),
        layout_(layout),
        descriptor_set_layout_(descriptor_set_layout),
        layout_bindings_(layout_bindings) {}
  VulkanPipeline(const rhi::ShaderCreateInfo& cinfo, VkPipeline pipeline, VkPipelineLayout layout,
                 VkDescriptorSetLayout descriptor_set_layout,
                 std::vector<VkDescriptorSetLayoutBinding>&& layout_bindings)
      : rhi::Pipeline(cinfo),
        pipeline_(pipeline),
        layout_(layout),
        descriptor_set_layout_(descriptor_set_layout),
        layout_bindings_(layout_bindings) {}

  VkPipeline pipeline_;

  // non-owning
  VkPipelineLayout layout_;
  VkDescriptorSetLayout descriptor_set_layout_;
  std::vector<VkDescriptorSetLayoutBinding> layout_bindings_;
  // end non-owning
};

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
