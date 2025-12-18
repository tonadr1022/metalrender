// clang-format off
#include "root_sig.h"
#include "material.h"
#include "shared_task2.h"
// clang-format on

[RootSignature(ROOT_SIGNATURE)] float4 main(VOut input) : SV_Target0 {
  return input.color;
  StructuredBuffer<M4Material> material_buf = ResourceDescriptorHeap[mat_buf_idx];
  M4Material material = material_buf[input.material_id];
  SamplerState samp = SamplerDescriptorHeap[LINEAR_SAMPLER_IDX];
  float4 albedo = material.color;
  albedo = float4(1, 1, 1, 1);
  if (material.albedo_tex_idx != 0) {
    Texture2D albedo_tex = ResourceDescriptorHeap[material.albedo_tex_idx];
    albedo *= albedo_tex.Sample(samp, input.uv);
  }
  if (albedo.a < 0.5) {
    discard;
  }
  return float4(albedo.xyz, 1.0);
}
