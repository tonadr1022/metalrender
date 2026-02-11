// clang-format off
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
#ifdef DEBUG_MODE
  // GlobalData globals = load_globals();
  uint render_mode = globals.render_mode;
  if (render_mode == DEBUG_RENDER_MODE_TRIANGLE_COLORS ||
      render_mode == DEBUG_RENDER_MODE_MESHLET_COLORS ||
      render_mode == DEBUG_RENDER_MODE_INSTANCE_COLORS) {
    fout.gbuffer_a = input.color;
    return fout;
  }
#endif
  M4Material material = material_buf[input.material_id];
  SamplerState samp = bindless_samplers[LINEAR_SAMPLER_IDX];
  float4 albedo = material.color;
  //  albedo = float4(1, 1, 1, 1);
  if (material.albedo_tex_idx != 0) {
    albedo *= (bindless_textures[material.albedo_tex_idx]).Sample(samp, input.uv);
  }
  if (albedo.a < 0.5) {
    //    discard;
  }
  fout.gbuffer_a = float4(albedo.xyz, 1.0);
  fout.gbuffer_b = float4(1, 0, 0, 1);
  return fout;
}
