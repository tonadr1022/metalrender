#include "root_sig.hlsl"
#include "shared_imgui.h"

float4 main(VOut input) : SV_Target {
  SamplerState samp = bindless_samplers[LINEAR_SAMPLER_IDX];
  float4 c;
  if (pc.flags & IMGUI_TEX_FLAG_FLOAT_BINDLESS) {
    float d = bindless_textures_float[pc.tex_idx].Sample(samp, input.uv).r;
    c = input.color * float4(d, d, d, 1.0);
  } else {
    Texture2D tex = bindless_textures[pc.tex_idx];
    c = input.color * tex.Sample(samp, input.uv);
  }
  return c;
}
