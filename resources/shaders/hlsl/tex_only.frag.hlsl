// clang-format off
#include "root_sig.hlsl"
#include "shared_tex_only.h"
#include "math.hlsli"
#include "shared_globals.h"
// clang-format on

struct VOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

float3 gamma_correct_undo(float3 c) { return pow(c, float3(2.2 / 1.0, 2.2 / 1.0, 2.2 / 1.0)); }

CONSTANT_BUFFER(GlobalData, globals, GLOBALS_SLOT);

float4 main(VOut input) : SV_Target {
  uint render_mode = globals.render_mode;
  Texture2D tex = bindless_textures[tex_idx];
  SamplerState samp = bindless_samplers[NEAREST_SAMPLER_IDX];
  if (render_mode == DEBUG_RENDER_MODE_NONE) {
    Texture2D gbuffer_b_tex = bindless_textures[gbuffer_b_idx];
    float4 gbuffer_b = gbuffer_b_tex.SampleLevel(samp, input.uv, 0);
    float3 L = normalize(float3(1, 2, 1));
    float3 N = gbuffer_b.rgb;
    float NdotL = dot(N, L);
    float4 albedo = tex.SampleLevel(samp, input.uv, 0);
    float ambient_intensity = 0.2;
    float3 ambient = albedo.xyz * ambient_intensity;
    return albedo;
    float4 light_out = float4(albedo.xyz * NdotL, albedo.a) + float4(ambient, 0);
    light_out = float4(tonemap(light_out.xyz), light_out.a);
    return light_out;
  } else {
    float4 color_out = color_mult * tex.SampleLevel(samp, input.uv, 0);
    color_out = float4(tonemap(color_out.xyz), color_out.a);
    return color_out;
  }
}
