#include "root_sig.h"
#include "material.h"
#include "../default_vertex.h"
#include "shared_basic_indirect.h"
#include "shared_indirect.h"

struct VOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation uint material_id : MATERIAL_ID;
};

struct DrawID {
    uint did;
    uint vert_id;
};

ConstantBuffer<DrawID> gDrawID : register(b1);

float3 rotate_quat(float3 v, float4 q) {
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

[RootSignature(ROOT_SIGNATURE)]
VOut vert_main(uint vert_id : SV_VertexID) {
    StructuredBuffer<InstanceData> instance_data_buf = ResourceDescriptorHeap[instance_data_buf_idx];
    InstanceData instance_data = instance_data_buf[gDrawID.did];
    StructuredBuffer<DefaultVertex> v_buf = ResourceDescriptorHeap[vert_buf_idx];
    DefaultVertex v = v_buf[vert_id + gDrawID.vert_id];
    VOut o;
    o.uv = v.uv;
    float3 pos = rotate_quat(instance_data.scale * v.pos.xyz, instance_data.rotation) + instance_data.translation;
    o.pos = mul(vp, float4(pos, 1.0));
    o.material_id = instance_data.mat_id;
    return o;
}

[RootSignature(ROOT_SIGNATURE)]
float4 frag_main(VOut input) : SV_Target0 {
    StructuredBuffer<M4Material> material_buf = ResourceDescriptorHeap[mat_buf_idx];
    M4Material material = material_buf[input.material_id];
    SamplerState samp = SamplerDescriptorHeap[LINEAR_SAMPLER_IDX];
    float4 albedo = material.color;
    if (material.albedo_tex_idx != 0) {
        Texture2D albedo_tex = ResourceDescriptorHeap[material.albedo_tex_idx];
        albedo *= albedo_tex.Sample(samp, input.uv);
    }
    if (albedo.a < 0.5) {
        discard;
    }
    return float4(albedo.xyz , 1.0);
    
}
