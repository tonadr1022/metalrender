#ifndef SHARED_TEST_TASK_H
#define SHARED_TEST_TASK_H

#include "../shader_core.h"

struct IdxOffset {
  uint idx;
  uint offset_bytes;
};

cbuffer Task2PC HLSL_PC_REG {
  uint globals_buf_idx;
  uint globals_buf_offset_bytes;
  uint cull_data_idx;
  uint cull_data_offset_bytes;
  uint mesh_data_buf_idx;
  uint meshlet_buf_idx;
  uint meshlet_tri_buf_idx;
  uint meshlet_vertex_buf_idx;
  uint vertex_buf_idx;
  uint instance_data_buf_idx;
  uint mat_buf_idx;
  uint tt_cmd_buf_idx;
  uint draw_cnt_buf_idx;
  uint max_draws;
  uint max_meshlets;
  uint _pad;
};

#ifdef __HLSL__

#include "../shader_constants.h"

struct Payload {
  uint meshlet_indices[K_TASK_TG_SIZE];
};

struct VOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
  nointerpolation float4 color : COLOR;
  nointerpolation uint material_id : MATERIAL_ID;
};

#endif

#endif
