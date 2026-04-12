#pragma once

#include "gfx/rhi/GFXTypes.hpp"
#include "vulkan/vulkan_core.h"

namespace TENG_NAMESPACE {
namespace gfx::vk {

VkImageUsageFlags convert(rhi::TextureUsage usage);
VkPipelineStageFlags2 convert(rhi::PipelineStage stage);
VkAccessFlags2 convert(rhi::AccessFlags access);
VkImageLayout convert(rhi::ResourceLayout layout);
void augment_memory_barrier2_stages_for_access(VkPipelineStageFlags2& src_stage,
                                               VkAccessFlags2 src_access,
                                               VkPipelineStageFlags2& dst_stage,
                                               VkAccessFlags2 dst_access);
VkPrimitiveTopology convert_prim_topology(rhi::PrimitiveTopology top);
VkCullModeFlags convert(rhi::CullMode cull_mode);
VkCompareOp convert_compare_op(rhi::CompareOp compare_op);

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
