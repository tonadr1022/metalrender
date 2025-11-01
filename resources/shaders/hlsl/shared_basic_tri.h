#ifndef __HLSL__
#define __HLSL__ 1
#endif

#include "../shader_core.h"

cbuffer BasicTriPC HLSL_REG(b0) {
  float4x4 mvp;
  uint vert_buf_idx;
  uint mat_buf_idx;
  uint mat_buf_id;
};
