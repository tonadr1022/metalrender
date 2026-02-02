#include "VkUtil.hpp"

#include "gfx/rhi/GFXTypes.hpp"

namespace TENG_NAMESPACE {
namespace gfx::vk {

VkImageUsageFlags convert(rhi::TextureUsageFlags usage) {
  VkImageUsageFlags flags{};
  if (usage & rhi::TextureUsageStorage) {
    flags |= VK_IMAGE_USAGE_STORAGE_BIT;
  }
  if (usage & rhi::TextureUsageSample) {
    flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
  }
  if (usage & rhi::TextureUsageColorAttachment) {
    flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  }
  if (usage & rhi::TextureUsageDepthStencilAttachment) {
    flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  }
  if (usage & rhi::TextureUsageTransferSrc) {
    flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  }
  if (usage & rhi::TextureUsageTransferDst) {
    flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }
  return flags;
}

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
