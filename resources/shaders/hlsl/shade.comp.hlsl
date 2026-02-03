// clang-format off
#define COMPUTE_ROOT_SIG
#include "root_sig.hlsl"
#include "shared_shade.h"
// clang-format on

PUSHCONSTANT(ShadePC, pc);

[RootSignature(ROOT_SIGNATURE)][NumThreads(8, 8, 1)] void main(uint2 dtid : SV_DispatchThreadID) {
  if (dtid.x >= pc.img_dims.x || dtid.y >= pc.img_dims.y) {
    return;
  }

  RWTexture2D<float4> out_tex = bindless_rwtextures[pc.output_tex_idx];
  Texture2D input_tex = bindless_textures[pc.gbuffer_a_tex_idx];
  float4 color = input_tex.Load(int3(dtid, 0));
  out_tex[dtid] = color;
}
