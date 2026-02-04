#include "../root_sig.hlsl"

struct VOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

Texture2D<float4> read_tex : register(t0);

SamplerState sampler_linear_clamp : register(s100);

[RootSignature(ROOT_SIGNATURE)] float4 main(VOut input) : SV_Target {
  return read_tex.SampleLevel(sampler_linear_clamp, input.uv, 0);
}
