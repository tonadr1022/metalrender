#pragma once

#include "gfx/rhi/GFXTypes.hpp"
#include "vulkan/vulkan_core.h"

namespace TENG_NAMESPACE {
namespace gfx::vk {

VkImageUsageFlags convert(rhi::TextureUsageFlags usage);

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
