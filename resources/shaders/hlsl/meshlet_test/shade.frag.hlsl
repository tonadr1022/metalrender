// clang-format off
#include "../root_sig.hlsl"
#include "../shader_core.h"
#include "../shared_globals.h"
#include "../shared_csm.h"
#include "shared_meshlet_test_shade.h"
// clang-format on

struct VOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

CONSTANT_BUFFER(GlobalData, globals, GLOBALS_SLOT);
CONSTANT_BUFFER(ViewData, view_data, VIEW_DATA_SLOT);
CONSTANT_BUFFER(CSMData, csm_data, 4);

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

float calculate_shadow_factor(float3 world_pos, in CSMData csm_data, SamplerState samp,
                              out uint cascade_idx_out) {
  uint cascade_count = min(csm_data.num_cascades, CSM_MAX_CASCADES);
  if (cascade_count == 0) {
    cascade_idx_out = 0;
    return 1.0f;
  }
  float3 view_pos = mul(view_data.view, float4(world_pos, 1.0)).xyz;
  const float view_depth = abs(view_pos.z);
  const uint cascade_idx = select_cascade(csm_data, view_depth);
  cascade_idx_out = cascade_idx;

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
  Texture2DArray shadow_tex = bindless_textures2DArray[pc.shadow_depth_array_idx];
  const float shadow_depth = shadow_tex.SampleLevel(samp, float3(shadow_pos.xy, cascade_idx), 0).r;
  return (shadow_depth < shadow_pos.z - bias) ? 0.0f : 1.0f;
}

float4 main(VOut input) : SV_Target {
  SamplerState samp = bindless_samplers[NEAREST_SAMPLER_IDX];
  Texture2D gbuffer_a_tex = bindless_textures[pc.gbuffer_a_idx];
  Texture2D gbuffer_b_tex = bindless_textures[pc.gbuffer_b_idx];
  Texture2D<float> depth_tex = bindless_textures_float[pc.depth_idx];
  float4 albedo = gbuffer_a_tex.SampleLevel(samp, input.uv, 0);
  float3 normal = gbuffer_b_tex.SampleLevel(samp, input.uv, 0).xyz * 2.0 - 1.0;
  normal = normalize(normal);

  float depth = depth_tex.SampleLevel(samp, input.uv, 0).r;
  float2 clip_xy = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);
  float4 clip_pos = float4(clip_xy, depth, 1.0);
  float4 wpos_pre_div = mul(view_data.inv_vp, clip_pos);
  float3 world_pos = wpos_pre_div.xyz / wpos_pre_div.w;

  uint cascade_idx = 0;
  float shadow_factor = calculate_shadow_factor(world_pos, csm_data, samp, cascade_idx);

  float3 L = normalize(globals.diffuse_light_dir_world.xyz);
  float ndotl = max(dot(normal, L), 0.0);
  float3 ambient = albedo.rgb * 0.02;
  float3 lit = albedo.rgb * ndotl * shadow_factor + ambient;

  if (globals.render_mode == DEBUG_RENDER_MODE_CSM_CASCADE_COLORS) {
    float4 colors[CSM_MAX_CASCADES] = {
        float4(1.0, 0.0, 0.0, 1.0),
        float4(0.0, 1.0, 0.0, 1.0),
        float4(0.0, 0.0, 1.0, 1.0),
        float4(1.0, 1.0, 0.0, 1.0),
    };
    return colors[min(cascade_idx, CSM_MAX_CASCADES - 1)];
  }

  float4 color = float4(lit, albedo.a);

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