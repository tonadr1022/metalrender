#include "../root_sig.hlsl"

struct VOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

Texture2D<float4> read_tex : register(t0);

SamplerState sampler_linear_clamp : register(s100);

[RootSignature(ROOT_SIGNATURE)] float4 main(VOut input) : SV_Target {
  // float4 color = read_tex.Sample(sampler_linear_clamp, input.uv);
  //  float4 color = read_tex.SampleLevel(bindless_samplers[NEAREST_SAMPLER_IDX], input.uv, 0);
  // Calculate pixel coordinates from UV

  int2 texSize;
  read_tex.GetDimensions(texSize.x, texSize.y);
  int2 pixelCoords = int2(input.uv * float2(texSize));
  float4 color = read_tex.Load(int3(0, 0, 0));

  if (color.a < 0.1) {
    discard;
  }
  return color;
}
