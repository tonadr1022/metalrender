// clang-format off
#include "root_sig.hlsl"
#include "material.h"
#include "shared_forward_meshlet.h"
#include "shared_globals.h"
// clang-format on

[RootSignature(ROOT_SIGNATURE)] float4 main(VOut input) : SV_Target0 {
#ifdef DEBUG_MODE
  ByteAddressBuffer global_data_buf = bindless_buffers[globals_buf_idx];
  GlobalData globals = global_data_buf.Load<GlobalData>(globals_buf_offset_bytes);
  uint render_mode = globals.render_mode;
  if (render_mode == DEBUG_RENDER_MODE_TRIANGLE_COLORS ||
      render_mode == DEBUG_RENDER_MODE_MESHLET_COLORS ||
      render_mode == DEBUG_RENDER_MODE_INSTANCE_COLORS) {
    return input.color;
  }
#endif
  M4Material material =
      bindless_buffers[mat_buf_idx].Load<M4Material>(input.material_id * sizeof(M4Material));
  SamplerState samp = bindless_samplers[LINEAR_SAMPLER_IDX];
  float4 albedo = material.color;
  albedo = float4(1, 1, 1, 1);
  if (material.albedo_tex_idx != 0) {
    albedo *= (bindless_textures[material.albedo_tex_idx]).Sample(samp, input.uv);
  }
  if (albedo.a < 0.5) {
    discard;
  }
  return float4(albedo.xyz, 1.0);
}
