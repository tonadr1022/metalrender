#pragma once

#include <Metal/MTLTexture.hpp>

#include "gfx/Texture.hpp"

class MetalTexture : public rhi::Texture {
 public:
  MetalTexture(const rhi::TextureDesc& desc, uint32_t gpu_slot, MTL::Texture* tex)
      : rhi::Texture(desc, gpu_slot), tex_(tex) {}
  MetalTexture() = default;
  [[nodiscard]] MTL::Texture* texture() const { return tex_; }

 private:
  MTL::Texture* tex_{};
};
