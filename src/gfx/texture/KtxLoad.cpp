#include "KtxLoad.hpp"

#include <ktx.h>

#include "VkFormatEnum.hpp"
#include "core/Logger.hpp"
#include "gfx/GFXTypes.hpp"

namespace gfx {

namespace {

rhi::TextureFormat convert(VkFormat format) {
  switch (format) {
    case VK_FORMAT_ASTC_4x4_SRGB_BLOCK: {
      return rhi::TextureFormat::ASTC4x4SrgbBlock;
    }
    case VK_FORMAT_ASTC_4x4_UNORM_BLOCK: {
      return rhi::TextureFormat::ASTC4x4UnormBlock;
    }
    default: {
      ASSERT(0);
      return rhi::TextureFormat::Undefined;
    }
  }
}

}  // namespace

LoadKtxTextureResult load_ktx_texture(const std::filesystem::path &path) {
  LoadKtxTextureResult load_result{};
  ktxTexture2 *&texture = load_result.texture;
  KTX_error_code result = ktxTexture2_CreateFromNamedFile(
      path.string().c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture);
  if (result != KTX_SUCCESS) {
    LERROR("Failed to load KTX texture from {}: {}", path.string(), ktxErrorString(result));
  }
  auto transcodable = ktxTexture2_NeedsTranscoding(texture);
  if (transcodable) {
    result = ktxTexture2_TranscodeBasis(texture, KTX_TTF_ASTC_4x4_RGBA, KTX_TF_HIGH_QUALITY);
    if (result != KTX_SUCCESS) {
      LERROR("Failed to transcode KTX texture from {}: {}", path.string(), ktxErrorString(result));
      goto cleanup_failed_load;
    }
  }

  load_result.format = convert((VkFormat)texture->vkFormat);

  return load_result;

cleanup_failed_load:
  ktxTexture2_Destroy(texture);
  return load_result;
}

}  // namespace gfx
