#ifndef SHARED_DEPTH_REDUCE_H
#define SHARED_DEPTH_REDUCE_H

#include "../shader_core.h"

struct DepthReducePC {
  uint in_tex_dim_x;
  uint in_tex_dim_y;
  uint out_tex_dim_x;
  uint out_tex_dim_y;
};

PUSHCONSTANT(DepthReducePC, pc);

#endif
