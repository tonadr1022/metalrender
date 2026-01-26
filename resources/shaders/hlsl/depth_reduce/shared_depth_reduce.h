#ifndef SHARED_DEPTH_REDUCE_H
#define SHARED_DEPTH_REDUCE_H

#include "../shader_core.h"

cbuffer DepthReducePC HLSL_PC_REG {
  uint in_tex_dim_x;
  uint in_tex_dim_y;
  uint out_tex_dim_x;
  uint out_tex_dim_y;
};

#endif
