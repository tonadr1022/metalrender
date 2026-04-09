// clang-format off
#define MESH_SHADER_OUTPUT_UV 1
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
  fout.color = float4(albedo.rgb, 1.0);
  return fout;
}
