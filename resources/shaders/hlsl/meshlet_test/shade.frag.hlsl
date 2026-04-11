// clang-format off
#include "../root_sig.hlsl"
#include "../shader_core.h"
// clang-format on

struct VOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

Texture2D<float4> gbuffer_a_tex : register(t0);

float4 main(VOut input) : SV_Target {
  SamplerState samp = bindless_samplers[NEAREST_SAMPLER_IDX];
  float4 color = gbuffer_a_tex.Sample(samp, input.uv);
  //   return float4(1, 0, 0, 1);
  return color;
}