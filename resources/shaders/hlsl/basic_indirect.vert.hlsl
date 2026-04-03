// clang-format off
#define DRAW_COUNT_REQUIRED
#include "root_sig.hlsl"
#include "material.h"
#include "default_vertex.h"
#include "shared_basic_indirect.h"
#include "shared_indirect.h"
#include "shared_globals.h"
#include "math.hlsli"
// clang-format on

VOut main(uint vert_id : SV_VertexID, uint instance_id : SV_InstanceID) {
  ViewData view_data = bindless_buffers[pc.view_data_buf_idx].Load<ViewData>(pc.view_data_buf_offset);
  InstanceData instance_data = bindless_buffers[pc.instance_data_buf_idx].Load<InstanceData>(
      GetDrawId() * sizeof(InstanceData));
  DefaultVertex v = bindless_buffers[pc.vert_buf_idx].Load<DefaultVertex>((vert_id + GetVertexIndex()) *
                                                                          sizeof(DefaultVertex));
  VOut o;
  o.uv = v.uv;
  float3 pos = rotate_quat(instance_data.scale * v.pos.xyz, instance_data.rotation) +
               instance_data.translation;
  o.pos = mul(view_data.vp, float4(pos, 1.0));
  o.normal = normalize(v.normal);
  o.material_id = instance_data.mat_id;
  return o;
}
