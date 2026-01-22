#ifndef SHARED_GLOBALS_H
#define SHARED_GLOBALS_H

#include "shader_core.h"

#define DEBUG_RENDER_MODE_NONE 0
#define DEBUG_RENDER_MODE_DEPTH_REDUCE_MIPS 1
#define DEBUG_RENDER_MODE_MESHLET_COLORS 2
#define DEBUG_RENDER_MODE_TRIANGLE_COLORS 3
#define DEBUG_RENDER_MODE_INSTANCE_COLORS 4
#define DEBUG_RENDER_MODE_COUNT 5

struct GlobalData {
  uint render_mode;
  uint _padding[3];
  float4x4 vp;
  float4x4 view;
  float4x4 proj;
  float4 camera_pos;
};

#endif
