#pragma once

#include <filesystem>

#include "gfx/GFXTypes.hpp"

struct ktxTexture2;

namespace gfx {

struct LoadKtxTextureResult {
  ktxTexture2* texture;
  rhi::TextureFormat format;
};

LoadKtxTextureResult load_ktx_texture(const std::filesystem::path& path);

}  // namespace gfx
