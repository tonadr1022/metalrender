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

uint select_cascade(in CSMData csm_data, float view_depth) {
  uint cascade_count = min(csm_data.num_cascades, CSM_MAX_CASCADES);
  if (cascade_count <= 1) {
    return 0;
  }

  uint cascade_idx = 0;
  [unroll] for (uint i = 0; i < CSM_MAX_CASCADES - 1; i++) {
    if (i >= cascade_count - 1) {
      break;
    }
    if (view_depth > csm_data.cascade_levels[i]) {
      cascade_idx = i + 1;
    }
  }
  return cascade_idx;
}

float calculate_shadow_factor(float3 world_pos, in CSMData csm_data, SamplerState samp) {
  uint cascade_count = min(csm_data.num_cascades, CSM_MAX_CASCADES);
  if (cascade_count == 0) {
    return 1.0f;
  }

  float3 view_pos = mul(view_data.view, float4(world_pos, 1.0)).xyz;
  float view_depth = abs(view_pos.z);
  uint cascade_idx = select_cascade(csm_data, view_depth);

  float4 shadow_pos = mul(csm_data.light_vp_matrices[cascade_idx], float4(world_pos, 1.0));
  shadow_pos.xyz /= shadow_pos.w;
  shadow_pos.xy = shadow_pos.xy * 0.5 + 0.5;

  if (shadow_pos.x < 0.0 || shadow_pos.x > 1.0 || shadow_pos.y < 0.0 || shadow_pos.y > 1.0 ||
      shadow_pos.z < 0.0 || shadow_pos.z > 1.0) {
    return 1.0f;
  }

  float bias = csm_data.biases.x;
  if (cascade_count > 1) {
    float t = cascade_idx / float(cascade_count - 1);
    bias = lerp(csm_data.biases.x, csm_data.biases.y, t);
  }

  float shadow_depth = shadow_tex_array.SampleLevel(samp, float3(shadow_pos.xy, cascade_idx), 0).r;
  if (shadow_depth < shadow_pos.z - bias) {
    return 0.0f;
  }
  return 1.0f;
}

float4 main(VOut input) : SV_Target {
  uint render_mode = globals.render_mode;
  Texture2D tex = bindless_textures[pc.tex_idx];
  SamplerState samp = bindless_samplers[NEAREST_SAMPLER_IDX];
  Texture2D depth_tex = bindless_textures[pc.depth_tex_idx];
  if (render_mode == DEBUG_RENDER_MODE_NONE) {
    Texture2D gbuffer_b_tex = bindless_textures[pc.gbuffer_b_idx];
    float4 gbuffer_b = gbuffer_b_tex.SampleLevel(samp, input.uv, 0);
    float3 L = normalize(float3(1, 2, 1));
    float3 N = gbuffer_b.rgb;
    float NdotL = dot(N, L);
    if (pc.shadows_enabled) {
      float2 uv = input.uv;
      float depth = depth_tex.SampleLevel(samp, uv, 0).r;
      float2 clip_xy = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
      float4 clip_pos = float4(clip_xy, depth, 1.0);
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
  } else if (render_mode == DEBUG_RENDER_MODE_CSM_CASCADE_COLORS) {
    const CSMData csm_data = csm_data_buf[0];
    uint cascade_count = min(csm_data.num_cascades, CSM_MAX_CASCADES);
    if (cascade_count == 0) {
      return float4(0.0, 0.0, 0.0, 1.0);
    }

    float2 uv = input.uv;
    float depth = depth_tex.SampleLevel(samp, uv, 0).r;
    // Reconstruct NDC
    float2 ndc_xy = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 ndc_pos = float4(ndc_xy, depth, 1.0);

    float4 wpos_pre_divide = mul(view_data.inv_vp, ndc_pos);
    float3 world_pos = wpos_pre_divide.xyz / wpos_pre_divide.w;
    float3 view_pos = mul(view_data.view, float4(world_pos, 1.0)).xyz;
    float view_depth = abs(view_pos.z);

    // float4 view_pos_h = mul(view_data.inv_proj, ndc_pos);
    // float3 view_pos = view_pos_h.xyz / view_pos_h.w;
    // float view_depth = abs(view_pos.z);

    uint cascade_idx = select_cascade(csm_data, view_depth);
    cascade_idx = min(cascade_idx, cascade_count - 1);

    float4 colors[CSM_MAX_CASCADES] = {
        float4(1.0, 0.0, 0.0, 1.0),
        float4(0.0, 1.0, 0.0, 1.0),
        float4(0.0, 0.0, 1.0, 1.0),
        float4(1.0, 1.0, 0.0, 1.0),
    };
    // return float4(float3(view_depth, view_depth, view_depth), 1.0);
    return colors[cascade_idx];
  } else if (render_mode == DEBUG_RENDER_MODE_SECONDARY_VIEW) {
    float4 color_out = pc.color_mult * tex.SampleLevel(samp, input.uv, 0);
    return color_out;
  } else {
    float4 color_out = pc.color_mult * tex.SampleLevel(samp, input.uv, 0);
    color_out = float4(tonemap(color_out.xyz), color_out.a);
    return color_out;
  }
}
