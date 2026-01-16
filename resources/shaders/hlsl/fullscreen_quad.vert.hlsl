#include "root_sig.h"

struct VOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

[RootSignature(ROOT_SIGNATURE)] VOut main(uint vert_id : SV_VertexID) {
  VOut o;
  o.uv = float2((vert_id << 1) & 2, vert_id & 2);
  o.pos = float4(o.uv.x * 2.0 - 1.0, 1.0 - o.uv.y * 2.0, 0.1, 1.0);
  return o;
}
