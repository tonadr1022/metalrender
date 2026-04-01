#ifndef SHARED_TEX_ONLY_H
#define SHARED_TEX_ONLY_H

#include "shader_core.h"

cbuffer TexOnlyPC HLSL_PC_REG {
  float4 color_mult;
  uint2 img_dims;
  uint tex_idx;
  uint gbuffer_b_idx;
  uint depth_tex_idx;
  uint mip_level;
  uint shadows_enabled;
};
#endif
