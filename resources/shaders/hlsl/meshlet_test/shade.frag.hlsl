// clang-format off
#include "../root_sig.hlsl"
#include "../shader_core.h"
#include "shared_meshlet_test_shade.h"
// clang-format on

struct VOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

float4 main(VOut input) : SV_Target {
  SamplerState samp = bindless_samplers[NEAREST_SAMPLER_IDX];
  Texture2D gbuffer_a_tex = bindless_textures[pc.gbuffer_a_idx];
  float4 color = gbuffer_a_tex.Sample(samp, input.uv);

  // Bottom-left corner: visualize depth pyramid mip (reverse-Z depth, brighter = closer).
  if (pc.pyramid_base_w > 0u && pc.depth_pyramid_view_idx != INVALID_TEX_ID) {
    const float corner = 0.28;
    if (input.uv.x < corner && input.uv.y < corner) {
      float2 corner_uv = float2(input.uv.x / corner, input.uv.y / corner);
      float d =
          bindless_textures_float[pc.depth_pyramid_view_idx].SampleLevel(samp, corner_uv, 0).r;
      float3 viz = float3(d, d, d);
      color.rgb = lerp(color.rgb, viz, 0.85);
    }
  }

  return color;
}