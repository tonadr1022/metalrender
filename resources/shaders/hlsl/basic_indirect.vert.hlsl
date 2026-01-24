// clang-format off
#include "root_sig.hlsl"
#include "material.h"
#include "default_vertex.h"
#include "shared_basic_indirect.h"
#include "shared_indirect.h"
#include "math.hlsli"
// clang-format on

struct DrawID {
  uint did;
  uint vert_id;
};

ConstantBuffer<DrawID> gDrawID : register(b1);

[RootSignature(ROOT_SIGNATURE)] VOut main(uint vert_id : SV_VertexID,
                                          uint instance_id : SV_InstanceID) {
  InstanceData instance_data = bindless_buffers[instance_data_buf_idx].Load<InstanceData>(
      gDrawID.did * sizeof(InstanceData));
  DefaultVertex v = bindless_buffers[vert_buf_idx].Load<DefaultVertex>((vert_id + gDrawID.vert_id) *
                                                                       sizeof(DefaultVertex));
  VOut o;
  o.uv = v.uv;
  float3 pos = rotate_quat(instance_data.scale * v.pos.xyz, instance_data.rotation) +
               instance_data.translation;
  o.pos = mul(vp, float4(pos, 1.0));
  o.material_id = instance_data.mat_id;
  return o;
}
