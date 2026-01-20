// clang-format off
#include "root_sig.h"
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
  StructuredBuffer<InstanceData> instance_data_buf = ResourceDescriptorHeap[instance_data_buf_idx];
  InstanceData instance_data = instance_data_buf[gDrawID.did];

  StructuredBuffer<DefaultVertex> v_buf = ResourceDescriptorHeap[vert_buf_idx];
  DefaultVertex v = v_buf[vert_id + gDrawID.vert_id];
  VOut o;
  o.uv = v.uv;
  float3 pos = rotate_quat(instance_data.scale * v.pos.xyz, instance_data.rotation) +
               instance_data.translation;
  o.pos = mul(vp, float4(pos, 1.0));
  o.material_id = instance_data.mat_id;
  return o;
}
