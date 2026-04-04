#include "root_sig.hlsl"
#include "shared_test_cube_bindless.h"

float4 main(VOut input) : SV_Target0 {
  Texture2D tex = bindless_textures[pc.tex_idx];
  SamplerState samp = bindless_samplers[LINEAR_SAMPLER_IDX];
  return tex.Sample(samp, input.uv);
}
