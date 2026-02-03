#pragma once

#include "gfx/rhi/GFXTypes.hpp"
#include "vulkan/vulkan_core.h"

namespace TENG_NAMESPACE {
namespace gfx::vk {

VkImageUsageFlags convert(rhi::TextureUsageFlags usage);
VkPipelineStageFlags2 convert(rhi::PipelineStage stage);
VkAccessFlags2 convert(rhi::AccessFlags access);

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
