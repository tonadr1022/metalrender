#pragma once

#include "glm/ext/vector_uint3.hpp"

namespace rhi {

enum class TextureFormat {
  Undefined,
  R8G8B8A8Srgb,
  R8G8B8A8Unorm,
  D32float,
};

enum class StorageMode { GPUOnly, CPUAndGPU, CPUOnly, Default };

enum TextureUsage {
  TextureUsageNone,
  TextureUsageShaderRead,
  TextureUsageShaderWrite,
  TextureUsageRenderTarget
};

using DefaultIndexT = uint32_t;

struct TextureDesc {
  TextureFormat format{TextureFormat::Undefined};
  StorageMode storage_mode{StorageMode::GPUOnly};
  TextureUsage usage{TextureUsageNone};
  glm::uvec3 dims{1};
  uint32_t mip_levels{1};
  uint32_t array_length{1};
  bool alloc_gpu_slot{};
};

struct BufferDesc {
  StorageMode storage_mode{StorageMode::GPUOnly};
  size_t size{};
  bool alloc_gpu_slot{false};
};

}  // namespace rhi
