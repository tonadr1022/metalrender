#include "editor/EditorViewportTarget.hpp"

#include <algorithm>
#include <cstdint>

#include "gfx/rhi/GFXTypes.hpp"

namespace teng::editor {

glm::uvec2 clamp_editor_viewport_extent(glm::uvec2 extent) {
  return {std::max(extent.x, k_editor_viewport_min_extent.x),
          std::max(extent.y, k_editor_viewport_min_extent.y)};
}

// TODO: this seems like a smell
void EditorViewportTarget::release(gfx::rhi::Device& device) {
  // if (!color_.handle.is_valid()) {
  //   return;
  // }
  // for (const gfx::rhi::TextureViewHandle view : color_.views) {
  //   device.destroy(color_.handle, view);
  // }
  // color_.views.clear();
  // color_ = {};
}

void EditorViewportTarget::ensure_size(gfx::rhi::Device& device, gfx::rhi::TextureFormat format,
                                       glm::uvec2 extent) {
  extent = clamp_editor_viewport_extent(extent);
  if (extent == extent_ && color_.handle.is_valid()) {
    return;
  }

  release(device);
  extent_ = extent;
  color_ = gfx::rhi::TexAndViewHolder(device.create_tex_h({
      .format = format,
      .usage = gfx::rhi::TextureUsage::ColorAttachment | gfx::rhi::TextureUsage::Sample,
      .dims = {extent.x, extent.y, 1},
      .name = "editor_viewport_color",
  }));
  color_.views.push_back(device.create_tex_view(color_.handle, 0, 1, 0, 1));
}

uint32_t EditorViewportTarget::bindless_view_idx(gfx::rhi::Device& device) const {
  if (!color_.handle.is_valid() || color_.views.empty()) {
    return UINT32_MAX;
  }
  return device.get_tex_view_bindless_idx(color_.handle, color_.views.front());
}

}  // namespace teng::editor
