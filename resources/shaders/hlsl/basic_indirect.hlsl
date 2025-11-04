#include "root_sig.h"
#include "material.h"
#include "../default_vertex.h"
#include "shared_basic_indirect.h"
#include "shared_indirect.h"

#include "bindless.hlsli"

struct VOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation uint material_id : MATERIAL_ID;
};

struct DrawID {
    uint did;
};

ConstantBuffer<DrawID> gDrawID : register(b1);

[RootSignature(ROOT_SIGNATURE)]
VOut vert_main(uint vert_id : SV_VertexID, uint instance_id : SV_InstanceID) {
    StructuredBuffer<InstData> instance_data_buf = ResourceDescriptorHeap[instance_data_buf_idx];
    InstData instance_data = instance_data_buf[gDrawID.did];
    StructuredBuffer<DefaultVertex> v_buf = ResourceDescriptorHeap[vert_buf_idx];
    DefaultVertex v = v_buf[vert_id];
    VOut o;
    o.uv = v.uv;
    o.pos = mul(vp, mul(instance_data.model, float4(v.pos.xyz, 1.0)));
    o.material_id = gDrawID.did;
    //o.material_id = instance_data.material_id;
    return o;
}

#define UINT32_MAX 4294967295

[RootSignature(ROOT_SIGNATURE)]
float4 frag_main(VOut input) : SV_Target0 {
    StructuredBuffer<M4Material> material_buf = ResourceDescriptorHeap[mat_buf_idx];
    M4Material material = material_buf[input.material_id];
    float4 albedo = float4(1,1,1,1);
    Texture2D albedo_tex = ResourceDescriptorHeap[material.albedo_tex_idx];
    SamplerState samp = SamplerDescriptorHeap[0];
    if (material.albedo_tex_idx != UINT32_MAX) {
        albedo *= albedo_tex.Sample(samp, input.uv);
    }
    return float4(albedo.xyz * material.color.rgb, 1.0);
    
}
