#pragma once

#include <span>

#include "Texture.hpp"

namespace rhi {

class Device {
 public:
  virtual ~Device() = default;
  [[nodiscard]] virtual void* get_native_device() const = 0;
  virtual void shutdown() = 0;

  virtual rhi::Texture create_texture(const rhi::TextureDesc& desc) = 0;
  virtual void generate_mipmaps(std::span<rhi::Texture> textures) = 0;
  virtual void generate_mipmaps(const rhi::Texture& textures) = 0;
};

}  // namespace rhi
