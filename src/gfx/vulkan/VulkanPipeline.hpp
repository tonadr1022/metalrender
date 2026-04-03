#pragma once

#include <vector>

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
                 std::vector<VkDescriptorSetLayoutBinding>&& layout_bindings,
                 size_t render_target_info_hash, uint32_t bindless_first_set,
                 std::vector<VkDescriptorSet>&& bindless_descriptor_sets,
                 VkShaderStageFlags push_constant_stages)
      : rhi::Pipeline(ginfo),
        pipeline_(pipeline),
        render_target_info_hash_(render_target_info_hash),
        layout_(layout),
        descriptor_set_layout_(descriptor_set_layout),
        layout_bindings_(std::move(layout_bindings)),
        bindless_first_set_(bindless_first_set),
        bindless_descriptor_sets_(std::move(bindless_descriptor_sets)),
        push_constant_stages_(push_constant_stages) {}
  VulkanPipeline(const rhi::ShaderCreateInfo& cinfo, VkPipeline pipeline, VkPipelineLayout layout,
                 VkDescriptorSetLayout descriptor_set_layout,
                 std::vector<VkDescriptorSetLayoutBinding>&& layout_bindings,
                 uint32_t bindless_first_set,
                 std::vector<VkDescriptorSet>&& bindless_descriptor_sets,
                 VkShaderStageFlags push_constant_stages)
      : rhi::Pipeline(cinfo),
        pipeline_(pipeline),
        layout_(layout),
        descriptor_set_layout_(descriptor_set_layout),
        layout_bindings_(std::move(layout_bindings)),
        bindless_first_set_(bindless_first_set),
        bindless_descriptor_sets_(std::move(bindless_descriptor_sets)),
        push_constant_stages_(push_constant_stages) {}

  VkPipeline pipeline_{};
  size_t render_target_info_hash_{};

  // non-owning
  VkPipelineLayout layout_{};
  VkDescriptorSetLayout descriptor_set_layout_{};
  std::vector<VkDescriptorSetLayoutBinding> layout_bindings_;
  uint32_t bindless_first_set_{0};
  std::vector<VkDescriptorSet> bindless_descriptor_sets_;
  VkShaderStageFlags push_constant_stages_{VK_SHADER_STAGE_ALL};
  // end non-owning
};

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
