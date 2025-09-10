#pragma once

#include <cstddef>
#include <cstdint>
#include <glm/vec3.hpp>

#include "GFXTypes.hpp"

namespace rhi {

struct TextureDesc {
  TextureFormat format;
  TextureUsage usage;
  StorageMode storage_mode;
  glm::uvec3 dims;
  uint32_t mip_levels{1};
  uint32_t array_length{1};
  std::string name;
};

size_t bytes_per_element(TextureFormat format) {
  switch (format) {
    case TextureFormat::R8G8B8A8Srgb:
    case TextureFormat::R8G8B8A8Unorm:
      return 4;
    default:
      assert(0);
      return 0;
  }
}

struct Texture {
  Texture() = default;
  explicit Texture(TextureDesc desc) : desc(std::move(desc)) {}
  [[nodiscard]] const TextureDesc& get_desc() const { return desc; }
  const TextureDesc desc{};
  std::shared_ptr<void> internal_state{};
  // TODO: min alignment
  constexpr size_t bytes_per_row() { return bytes_per_element(desc.format) * desc.dims.x; }
};

}  // namespace rhi
