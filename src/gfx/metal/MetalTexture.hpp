#pragma once

#include <Metal/MTLTexture.hpp>

#include "gfx/Texture.hpp"

class MetalTexture : public rhi::Texture {
 public:
  MetalTexture(const rhi::TextureDesc& desc, uint32_t gpu_slot, MTL::Texture* tex,
               bool is_drawable_tex = false)
      : rhi::Texture(desc, gpu_slot), tex_(tex), is_drawable_tex_(is_drawable_tex) {}
  MetalTexture() = default;
  [[nodiscard]] MTL::Texture* texture() const { return tex_; }
  [[nodiscard]] bool is_drawable_tex() const { return is_drawable_tex_; }

  struct TexView {
    MTL::Texture* tex;
    uint32_t bindless_idx;
  };
  std::vector<TexView> tex_views;

 private:
  MTL::Texture* tex_{};
  bool is_drawable_tex_{};
};
