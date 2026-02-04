// clang-format off
#include "../root_sig.hlsl"
#include "../default_vertex.h"
// clang-format on

struct VOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

StructuredBuffer<DefaultVertex> vertices : register(t1);

[RootSignature(ROOT_SIGNATURE)] VOut main(uint vert_id : SV_VertexID) {
  DefaultVertex v = vertices[vert_id];
  VOut o;
  o.pos = v.pos;
  o.uv = v.uv;
  return o;
}
