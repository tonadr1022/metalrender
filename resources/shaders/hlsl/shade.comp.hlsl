// clang-format off
#define COMPUTE_ROOT_SIG
#include "root_sig.hlsl"
#include "shared_shade.h"
#include "shared_csm.h"
#include "math.hlsli"
// clang-format on

PUSHCONSTANT(ShadePC, pc);

StructuredBuffer<CSMData> csm_data_buf : register(t4);
Texture2DArray shadow_tex_array : register(t5);

[NumThreads(8, 8, 1)] void main(uint2 dtid
                                : SV_DispatchThreadID) {
  if (dtid.x >= pc.img_dims.x || dtid.y >= pc.img_dims.y) {
    return;
  }

  RWTexture2D<float4> out_tex = bindless_rwtextures[pc.output_tex_idx];
  Texture2D input_tex = bindless_textures[pc.gbuffer_a_tex_idx];
  float4 color = input_tex.Load(int3(dtid, 0));
  // color.rgb = gamma_correct(color.rgb);
  out_tex[dtid] = color;
}
