#pragma once

#include <cstdint>

#include "core/Config.hpp"
#include "gfx/rhi/Config.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "gfx/rhi/Texture.hpp"

namespace TENG_NAMESPACE {

namespace gfx {

enum class RenderViewId : uint32_t { Invalid = UINT32_MAX };

struct IdxOffset {
  rhi::BufferHandle buf;
  uint32_t idx;
  uint32_t offset_bytes;
};

struct RenderView {
  IdxOffset data_buf_info{};
  IdxOffset cull_data_buf_info{};
  rhi::TexAndViewHolder depth_pyramid_tex;
  rhi::BufferHandleHolder instance_vis_buf;
  std::array<rhi::BufferHandleHolder, k_max_frames_in_flight> draw_cmd_count_buf_readback;
  // std::array<rhi::BufferHandleHolder, k_max_frames_in_flight> draw_cmd_count_buf;
};

}  // namespace gfx

}  // namespace TENG_NAMESPACE