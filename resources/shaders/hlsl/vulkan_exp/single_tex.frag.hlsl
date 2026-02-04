#include "../root_sig.hlsl"

struct VOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

Texture2D<float4> read_tex : register(t0);

SamplerState sampler_linear_clamp : register(s100);

[RootSignature(ROOT_SIGNATURE)] float4 main(VOut input) : SV_Target {
  float4 color = read_tex.Sample(sampler_linear_clamp, input.uv);
  if (color.a < 0.1) {
    discard;
  }
  return color;
}
