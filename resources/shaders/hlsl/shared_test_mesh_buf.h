#ifndef SHARED_TEST_MESH_BUF_H
#define SHARED_TEST_MESH_BUF_H

#ifdef __HLSL__

struct MeshHelloVertex {
  float4 pos;
  float4 color;
};

struct VOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
};

#endif

#endif
