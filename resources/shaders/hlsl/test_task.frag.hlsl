// clang-format off
#include "root_sig.h"
#include "material.h"
#include "shared_task2.h"
#include "shared_globals.h"
// clang-format on

[RootSignature(ROOT_SIGNATURE)] float4 main(VOut input) : SV_Target0 {
#ifdef DEBUG_MODE
  ByteAddressBuffer global_data_buf = ResourceDescriptorHeap[globals_buf_idx];
  GlobalData globals = global_data_buf.Load<GlobalData>(globals_buf_offset_bytes);
  uint render_mode = globals.render_mode;
  if (render_mode == DEBUG_RENDER_MODE_TRIANGLE_COLORS ||
      render_mode == DEBUG_RENDER_MODE_MESHLET_COLORS ||
      render_mode == DEBUG_RENDER_MODE_INSTANCE_COLORS) {
    return input.color;
  }
#endif
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
