// clang-format off
#include "root_sig.h"
#include "shared_tex_only.h"
#include "math.hlsli"
// clang-format on

struct VOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

[RootSignature(ROOT_SIGNATURE)] float4 main(VOut input) : SV_Target {
  Texture2D tex = ResourceDescriptorHeap[tex_idx];
  SamplerState samp = SamplerDescriptorHeap[NEAREST_SAMPLER_IDX];
  float4 color = color_mult * tex.SampleLevel(samp, input.uv, 0);
  color = float4(tonemap(color.xyz), color.a);
  // color = float4(gamma_correct(color.xyz), color.a);
  return color;
}
