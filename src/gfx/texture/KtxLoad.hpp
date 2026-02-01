#pragma once

#include <filesystem>

#include "core/Config.hpp"
#include "gfx/rhi/GFXTypes.hpp"

struct ktxTexture2;

namespace TENG_NAMESPACE {

namespace gfx {

struct LoadKtxTextureResult {
  ktxTexture2* texture;
  rhi::TextureFormat format;
};

LoadKtxTextureResult load_ktx_texture(const std::filesystem::path& path);
LoadKtxTextureResult load_ktx_texture(const void* data, size_t data_size);

}  // namespace gfx

}  // namespace TENG_NAMESPACE
