#ifndef SHARED_GLOBALS_H
#define SHARED_GLOBALS_H

#include "shader_core.h"

#define DEBUG_RENDER_MODE_LIST(X)                             \
  X(None, DEBUG_RENDER_MODE_NONE, 0u)                         \
  X(DepthReduceMips, DEBUG_RENDER_MODE_DEPTH_REDUCE_MIPS, 1u) \
  X(SecondaryView, DEBUG_RENDER_MODE_SECONDARY_VIEW, 2u)      \
  X(MeshletColors, DEBUG_RENDER_MODE_MESHLET_COLORS, 3u)      \
  X(TriangleColors, DEBUG_RENDER_MODE_TRIANGLE_COLORS, 4u)    \
  X(InstanceColors, DEBUG_RENDER_MODE_INSTANCE_COLORS, 5u)    \
  X(Albedo, DEBUG_RENDER_MODE_ALBEDO, 6u)                     \
  X(Count, DEBUG_RENDER_MODE_COUNT, 7u)
#if defined(__cplusplus)

enum class DebugRenderMode : uint32_t {
#define DEBUG_RENDER_MODE_ENUM(name, define_name, value) name = (value),
  DEBUG_RENDER_MODE_LIST(DEBUG_RENDER_MODE_ENUM)
#undef DEBUG_RENDER_MODE_ENUM
};

inline const char* to_string(DebugRenderMode mode) {
#define DEBUG_RENDER_MODE_CASE(name, define_name, value) \
  case DebugRenderMode::name:                            \
    return #name;
  switch (mode) {
    DEBUG_RENDER_MODE_LIST(DEBUG_RENDER_MODE_CASE);
    default:
      return "";
  }
}
#endif

#ifdef __HLSL__
#define DEBUG_RENDER_MODE_CONST(name, define_name, value) static const uint define_name = value;
#else
#define DEBUG_RENDER_MODE_CONST(name, define_name, value) constexpr uint32_t define_name = value;
#endif
DEBUG_RENDER_MODE_LIST(DEBUG_RENDER_MODE_CONST)
#undef DEBUG_RENDER_MODE_CONST
#undef DEBUG_RENDER_MODE_LIST

struct GlobalData {
  uint render_mode;
  uint frame_num;
  uint meshlet_stats_enabled;
  uint _padding;
};

struct ViewData {
  float4x4 vp;
  float4x4 inv_vp;
  float4x4 view;
  float4x4 proj;
  float4 camera_pos;
};

#define GLOBALS_SLOT 3
#define VIEW_DATA_SLOT 2

#ifdef __HLSL__
#define load_globals() \
  (bindless_buffers[globals_buf_idx].Load<GlobalData>(globals_buf_offset_bytes))
#endif

#endif
