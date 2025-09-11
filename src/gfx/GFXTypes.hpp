#pragma once

#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"

struct DefaultVertex {
  glm::vec4 pos;
  glm::vec2 uv;
  glm::vec3 normal;
};
enum class TextureFormat { Undefined, R8G8B8A8Srgb, R8G8B8A8Unorm };

enum class StorageMode { GPUOnly, CPUAndGPU, CPUOnly };

struct TextureDesc {
  TextureFormat format{TextureFormat::Undefined};
  StorageMode storage_mode{StorageMode::GPUOnly};
  glm::uvec3 dims{1};
  uint32_t mip_levels{1};
  uint32_t array_length{1};
  void *data{};
};
