// clang-format off
#include "root_sig.hlsl"
#include "shared_tex_only.h"
#include "shared_csm.h"
#include "math.hlsli"
#include "shared_globals.h"
// clang-format on

struct VOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

float3 gamma_correct_undo(float3 c) { return pow(c, float3(2.2 / 1.0, 2.2 / 1.0, 2.2 / 1.0)); }

CONSTANT_BUFFER(GlobalData, globals, GLOBALS_SLOT);
CONSTANT_BUFFER(ViewData, view_data, VIEW_DATA_SLOT);

StructuredBuffer<CSMData> csm_data_buf : register(t0);
Texture2DArray shadow_tex_array : register(t1);

float calculate_shadow_factor(float3 world_pos, in CSMData csm_data, SamplerState samp) {
  for (uint i = 0; i < 1; i++) {
    float4 shadow_pos = mul(csm_data.light_vp_matrices[i], float4(world_pos, 1.0));
    shadow_pos.xyz /= shadow_pos.w;
    // [-1,1] to [0,1]
    shadow_pos.xy = shadow_pos.xy * 0.5 + 0.5;
    float shadow_depth = shadow_tex_array.SampleLevel(samp, float3(shadow_pos.xy, i), 0).r;
    if (shadow_depth < shadow_pos.z) {
      return 0.0f;
    }
  }
  return 1.0f;
}

float4 main(VOut input) : SV_Target {
  uint render_mode = globals.render_mode;
  Texture2D tex = bindless_textures[tex_idx];
  SamplerState samp = bindless_samplers[NEAREST_SAMPLER_IDX];
  Texture2D depth_tex = bindless_textures[depth_tex_idx];
  if (render_mode == DEBUG_RENDER_MODE_NONE) {
    Texture2D gbuffer_b_tex = bindless_textures[gbuffer_b_idx];
    float4 gbuffer_b = gbuffer_b_tex.SampleLevel(samp, input.uv, 0);
    float3 L = normalize(float3(1, 2, 1));
    float3 N = gbuffer_b.rgb;
    float NdotL = dot(N, L);
    if (shadows_enabled) {
      float2 uv = (float2(input.uv) + .5) / float2(img_dims);
      float depth = depth_tex.SampleLevel(samp, uv, 0).r;
      float4 clip_pos = float4(uv * 2. - 1., depth, 1.);
      float4 wpos_pre_divide = mul(view_data.inv_vp, clip_pos);
      float3 world_pos = wpos_pre_divide.xyz / wpos_pre_divide.w;
      float shadow_factor = calculate_shadow_factor(world_pos, csm_data_buf[0], samp);
      NdotL *= shadow_factor;
    }
    float4 albedo = tex.SampleLevel(samp, input.uv, 0);
    float ambient_intensity = 0.2;
    float3 ambient = albedo.xyz * ambient_intensity;
    float4 light_out = float4(albedo.xyz * NdotL, albedo.a) + float4(ambient, 0);
    light_out = float4(tonemap(light_out.xyz), light_out.a);
    return light_out;
  } else if (render_mode == DEBUG_RENDER_MODE_SECONDARY_VIEW) {
    float4 color_out = color_mult * tex.SampleLevel(samp, input.uv, 0);
    return color_out;
  } else {
    float4 color_out = color_mult * tex.SampleLevel(samp, input.uv, 0);
    color_out = float4(tonemap(color_out.xyz), color_out.a);
    return color_out;
  }
}
