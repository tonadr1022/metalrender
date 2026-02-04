#pragma once

#include "gfx/rhi/GFXTypes.hpp"
#include "vulkan/vulkan_core.h"

namespace TENG_NAMESPACE {
namespace gfx::vk {

VkImageUsageFlags convert(rhi::TextureUsageFlags usage);
VkPipelineStageFlags2 convert(rhi::PipelineStage stage);
VkAccessFlags2 convert(rhi::AccessFlags access);
VkPrimitiveTopology convert_prim_topology(rhi::PrimitiveTopology top);
VkCullModeFlags convert(rhi::CullMode cull_mode);

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
