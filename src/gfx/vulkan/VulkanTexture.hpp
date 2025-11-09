#pragma once

#include "gfx/Texture.hpp"

class VulkanTexture : public rhi::Texture {
 public:
  VulkanTexture(const rhi::TextureDesc& desc, uint32_t bindless_idx)
      : rhi::Texture(desc, bindless_idx) {}
  VulkanTexture() = default;
  ~VulkanTexture() = default;
};
