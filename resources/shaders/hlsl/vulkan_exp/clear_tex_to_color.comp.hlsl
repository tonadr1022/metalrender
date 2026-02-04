// clang-format off
#include "../root_sig.hlsl"
#include "../shader_core.h"
// clang-format on

struct PC {
  uint2 dims;
};

PUSHCONSTANT(PC, pc);

RWTexture2D<float4> out_tex : register(u0);

[RootSignature(ROOT_SIGNATURE)][NumThreads(8, 8, 1)] void main(uint2 dtid : SV_DispatchThreadID) {
  if (dtid.x >= pc.dims.x || dtid.y >= pc.dims.y) return;

  if (dtid.x < pc.dims.x / 2) {
    if (dtid.y < pc.dims.y / 2) {
      out_tex[dtid] = float4(1.0, 0.0, 0.0, 1.0);
    } else {
      out_tex[dtid] = float4(0.0, 1.0, 1.0, 1.0);
    }
  } else {
    out_tex[dtid] = float4(0.0, 0.0, 0.0, 0.0);
  }

  out_tex[dtid] = float4(1.0, 1.0, 1.0, 1.0);
}
