#pragma once

namespace rhi {

enum class StorageMode { GPUOnly, CPUAndGPU, CPUOnly };

enum class TextureFormat { Undefined, R8G8B8A8Srgb, R8G8B8A8Unorm };

enum TextureUsage {
  TextureUsage_None,
  TextureUsage_ShaderRead,
  TextureUsage_ShaderWrite,
  TextureUsage_RenderTarget
};

}  // namespace rhi
