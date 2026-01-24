#include "root_sig.hlsl"
#include "shared_imgui.h"

[RootSignature(ROOT_SIGNATURE)] float4 main(VOut input) : SV_Target {
  Texture2D tex = bindless_textures[tex_idx];
  SamplerState samp = bindless_samplers[LINEAR_SAMPLER_IDX];
  float4 c = input.color * tex.Sample(samp, input.uv);
  return c;
}
