#ifndef SHARED_SHADE_H
#define SHARED_SHADE_H

#include "../shader_core.h"

cbuffer ShadePC HLSL_PC_REG {
  uint gbuffer_a_tex_idx;
  uint depth_pyramid_tex_idx;
  uint view_mip;
  uint output_tex_idx;
  uint2 img_dims;
};

#endif
