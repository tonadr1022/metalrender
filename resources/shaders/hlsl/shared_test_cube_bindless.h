#ifndef SHARED_TEST_CUBE_BINDLESS_H
#define SHARED_TEST_CUBE_BINDLESS_H

#include "shader_core.h"

struct CubePC {
  float4x4 mvp;
  uint vert_buf_idx;
  uint tex_idx;
};

PUSHCONSTANT(CubePC, pc);

#ifdef __HLSL__

struct CubeVertex {
  float4 pos;
  float2 uv;
};

struct VOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

#endif

#endif
