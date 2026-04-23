// clang-format off
#define MESH_SHADER_OUTPUT_UV 1
#define MESH_SHADER_OUTPUT_NORMAL 1
#define MESH_SHADER_OUTPUT_MATERIAL 1
#include "root_sig.hlsl"
#include "material.h"
#include "shared_forward_meshlet.h"
#include "shared_globals.h"
// clang-format on

CONSTANT_BUFFER(GlobalData, globals, GLOBALS_SLOT);
StructuredBuffer<M4Material> material_buf : register(t11);

struct FOut {
  float4 color : SV_Target0;
};

FOut main(VOut input) {
  FOut fout;
  M4Material material = material_buf[input.material_id];
  SamplerState samp = bindless_samplers[LINEAR_SAMPLER_IDX];
  float4 albedo = material.color;
#ifdef MESH_SHADER_OUTPUT_UV
  if (material.albedo_tex_idx != INVALID_TEX_ID) {
    albedo *= bindless_textures[material.albedo_tex_idx].Sample(samp, input.uv);
  }
#endif
  float3 L = globals.diffuse_light_dir_world.xyz;
  float ndotl = 1.0;
  if (dot(L, L) > 1e-8) {
    float3 N = normalize(input.normal);
    L = normalize(L);
    ndotl = saturate(dot(N, L));
  }
  float3 ambient = float3(0.01, 0.01, 0.01) * albedo.rgb;
  fout.color = float4(min(albedo.rgb * ndotl + ambient, 1.0), albedo.a);
  return fout;
}
