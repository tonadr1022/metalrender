#ifndef SHARED_IMGUI_H
#define SHARED_IMGUI_H

#include "shader_core.h"

// Fragment: use bindless_textures_float when sampling R32F (e.g. depth pyramid mip views).
#define IMGUI_TEX_FLAG_FLOAT_BINDLESS (1 << 0)
#define IMGUI_FLAG_SRGB_COLOR (1 << 1)
// Fragment: sample bindless_textures2DArray; cascade / slice in bits 8-15 of flags.
#define IMGUI_TEX_FLAG_CSM_DEPTH_ARRAY (1 << 2)
#define IMGUI_CSM_LAYER_MASK 0xFF00

struct ImGuiPC {
  float4x4 proj;
  uint vert_buf_idx;
  uint tex_idx;
  uint flags;
};

PUSHCONSTANT(ImGuiPC, pc);

struct ImGuiVertex {
  float2 position;
  float2 tex_coords;
  uint color;
};

#ifdef __HLSL__

struct VOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
  float4 color : COLOR0;
};

#endif

#endif
