// clang-format off
#include "root_sig.hlsl"
#include "material.h"
#include "shared_basic_indirect.h"
// clang-format on

float4 main(VOut input) : SV_Target0 {
  M4Material material =
      bindless_buffers[mat_buf_idx].Load<M4Material>(input.material_id * sizeof(M4Material));
  SamplerState samp = SamplerDescriptorHeap[LINEAR_SAMPLER_IDX];
  float4 albedo = material.color;
  if (material.albedo_tex_idx != 0) {
    albedo *= (bindless_textures[material.albedo_tex_idx]).Sample(samp, input.uv);
  }
  if (albedo.a < 0.5) {
    discard;
  }
  return float4(albedo.xyz, 1.0);
}
