#ifndef SHARED_TEST_TASK_H
#define SHARED_TEST_TASK_H

#include "shader_core.h"

#define MESHLET_FRUSTUM_CULL_ENABLED_BIT (1 << 0)
#define MESHLET_CONE_CULL_ENABLED_BIT (1 << 1)
#define MESHLET_OCCLUSION_CULL_ENABLED_BIT (1 << 2)

cbuffer Task2PC HLSL_PC_REG {
  uint pass;
  uint flags;
};

#define MESHLET_VIS_BUF_SLOT 2

#ifdef __HLSL__

#include "shader_constants.h"

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
