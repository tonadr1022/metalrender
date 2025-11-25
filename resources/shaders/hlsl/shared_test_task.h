#ifndef SHARED_TEST_TASK_H
#define SHARED_TEST_TASK_H

#include "../shader_core.h"

cbuffer TestTaskPC HLSL_PC_REG {
  float4x4 vp;
  uint task_cmd_buf_idx;
  uint task_cmd_idx;
  uint meshlet_buf_idx;
  uint meshlet_tri_buf_idx;
  uint meshlet_vertex_buf_idx;
  uint vertex_buf_idx;
  uint instance_data_buf_idx;
  uint instance_data_idx;
};

#ifdef __HLSL__

#include "../shader_constants.h"

struct Payload {
  uint meshlet_indices[K_TASK_TG_SIZE];
};

struct VOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
};

#endif

#endif
