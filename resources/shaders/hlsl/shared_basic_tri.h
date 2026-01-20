#include "shader_core.h"

cbuffer BasicTriPC HLSL_PC_REG {
  float4x4 mvp;
  uint vert_buf_idx;
  uint mat_buf_idx;
  uint mat_buf_id;
};
