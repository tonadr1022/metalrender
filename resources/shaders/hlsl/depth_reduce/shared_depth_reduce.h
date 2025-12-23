#ifndef SHARED_DEPTH_REDUCE_H
#define SHARED_DEPTH_REDUCE_H

#include "../../shader_core.h"

cbuffer DepthReducePC HLSL_PC_REG {
  uint in_tex_idx;
  uint out_tex_idx;
  uint in_tex_mip_level;
  uint2 in_tex_dims;
};

#endif
