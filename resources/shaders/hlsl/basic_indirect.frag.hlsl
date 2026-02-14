// clang-format off
#include "root_sig.hlsl"
#include "material.h"
#include "shared_basic_indirect.h"
// clang-format on

struct FOut {
  float4 gbuffer_a : SV_Target0;
  float4 gbuffer_b : SV_Target1;
};
FOut main(VOut input) {
  FOut fout;
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
  fout.gbuffer_a = float4(albedo.xyz, 1.0);
  fout.gbuffer_b = float4(input.normal, 1);
  return fout;
}
