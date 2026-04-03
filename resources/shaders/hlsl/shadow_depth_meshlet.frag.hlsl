// clang-format off
#include "root_sig.hlsl"
#include "material.h"
// CSM mesh shader always outputs these; fragment interface must match or SPIR-V / validation
// warns and mesh outputs are effectively dropped.
#define MESH_SHADER_OUTPUT_UV 1
#define MESH_SHADER_OUTPUT_MATERIAL 1
#include "shared_forward_meshlet.h"
#include "shared_globals.h"
// clang-format on

CONSTANT_BUFFER(GlobalData, globals, GLOBALS_SLOT);
StructuredBuffer<M4Material> material_buf : register(t11);

void main(VOut input) {
#ifdef ALPHA_TEST
  M4Material material = material_buf[input.material_id];
  SamplerState samp = bindless_samplers[LINEAR_SAMPLER_IDX];
  float4 albedo = material.color;
  if (material.albedo_tex_idx != INVALID_TEX_ID) {
    albedo *= bindless_textures[material.albedo_tex_idx].Sample(samp, input.uv);
  }
  if (albedo.a < 0.5) {
    discard;
  }
#else
  // Keep varyings live so DXC does not drop fragment inputs (matches mesh stage locations).
  // float _iface = dot(input.uv, float2(1e-20, 1e-20)) + float(input.material_id) * 1e-30f;
  // clip(1.0 + _iface);
#endif
}
