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
  float4 gbuffer_a : SV_Target0;
  float4 gbuffer_b : SV_Target1;
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
  float3 n = float3(0.0, 1.0, 0.0);
#ifdef MESH_SHADER_OUTPUT_NORMAL
  n = normalize(input.normal);
#endif
  fout.gbuffer_a = albedo;
  fout.gbuffer_b = float4(n * 0.5 + 0.5, 1.0);
  return fout;
}
