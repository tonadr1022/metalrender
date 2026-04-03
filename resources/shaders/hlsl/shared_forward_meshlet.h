#ifndef SHARED_TEST_TASK_H
#define SHARED_TEST_TASK_H

#include "shader_core.h"

#define MESHLET_FRUSTUM_CULL_ENABLED_BIT (1 << 0)
#define MESHLET_CONE_CULL_ENABLED_BIT (1 << 1)
#define MESHLET_OCCLUSION_CULL_ENABLED_BIT (1 << 2)

struct Task2PC {
  // uint pass;
  uint flags;
  uint alpha_test_enabled;
  uint out_draw_count_buf_idx;
};

PUSHCONSTANT(Task2PC, pc);

#define MESHLET_VIS_BUF_SLOT 2

#ifdef __HLSL__

#include "shader_constants.h"

struct Payload {
  uint meshlet_indices[K_TASK_TG_SIZE];
};

struct VOut {
  float4 pos : SV_Position;
#ifdef MESH_SHADER_OUTPUT_NORMAL
  float3 normal : NORMAL;
#endif
#ifdef MESH_SHADER_OUTPUT_UV
  float2 uv : TEXCOORD0;
#endif
#ifdef MESH_SHADER_OUTPUT_COLOR
  nointerpolation float4 color : COLOR;
#endif
#ifdef MESH_SHADER_OUTPUT_MATERIAL
  nointerpolation uint material_id : MATERIAL_ID;
#endif
};

#endif

#endif
