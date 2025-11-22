#ifndef SHARED_IMGUI_H
#define SHARED_IMGUI_H

#include "../shader_core.h"

cbuffer ImGuiPC HLSL_PC_REG {
  float4x4 proj;
  uint vert_buf_idx;
  uint tex_idx;
};

struct ImGuiVertex {
  float2 position;
  float2 tex_coords;
  uint color;
};

#endif
