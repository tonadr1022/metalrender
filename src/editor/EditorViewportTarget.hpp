#pragma once

#include <glm/ext/vector_uint2.hpp>

#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/Texture.hpp"

namespace teng::editor {

inline constexpr glm::uvec2 k_editor_viewport_min_extent{8, 8};

[[nodiscard]] glm::uvec2 clamp_editor_viewport_extent(glm::uvec2 extent);

class EditorViewportTarget {
 public:
  void ensure_size(gfx::rhi::Device& device, gfx::rhi::TextureFormat format, glm::uvec2 extent);

  [[nodiscard]] gfx::rhi::TextureHandle handle() const { return color_.handle; }
  [[nodiscard]] bool valid() const { return color_.handle.is_valid(); }
  [[nodiscard]] uint32_t bindless_view_idx(gfx::rhi::Device& device) const;

  void release(gfx::rhi::Device& device);

 private:
  gfx::rhi::TexAndViewHolder color_;
  glm::uvec2 extent_{};
};

}  // namespace teng::editor
