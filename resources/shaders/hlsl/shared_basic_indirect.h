#ifndef SHARED_BASIC_INDIRECT_H
#define SHARED_BASIC_INDIRECT_H

#include "../shader_core.h"

cbuffer BasicIndirectPC HLSL_PC_REG {
  float4x4 vp;
  uint vert_buf_idx;
  uint instance_data_buf_idx;
  uint mat_buf_idx;
  uint inst_id;
};

#endif  // SHARED_BASIC_INDIRECT_H
