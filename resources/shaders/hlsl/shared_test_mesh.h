#ifndef SHARED_TEST_MESH_H
#define SHARED_TEST_MESH_H

#ifdef __HLSL__

struct VOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
};

#endif

#endif
