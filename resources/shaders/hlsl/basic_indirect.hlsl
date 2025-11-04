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
    InstData instance_data = BufferTable[instance_data_buf_idx].BUFLOAD(InstData, gDrawID.did);
    DefaultVertex v = BufferTable[vert_buf_idx].BUFLOAD(DefaultVertex, vert_id);
    VOut o;
    o.uv = v.uv;
    if (instance_id == 1) {
        o.pos = mul(vp, mul(instance_data.model, float4(v.pos.xyz, 1.0)));
    } else {
        o.pos = mul(vp, float4(v.pos.xyz, 1.0));
    }
    o.material_id = instance_data.material_id;
    return o;
}

#define UINT32_MAX 4294967295

[RootSignature(ROOT_SIGNATURE)]
float4 frag_main(VOut input) : SV_Target0 {
    M4Material material = BufferTable[mat_buf_idx].BUFLOAD(M4Material, input.material_id);
    float4 albedo = float4(1,1,1,1);
    if (material.albedo_tex_idx != UINT32_MAX) {
        albedo *= TextureTable[material.albedo_tex_idx].Sample(SamplerTable[0], input.uv);
    }
    return float4(albedo.xyz * material.color.rgb, 1.0);
    
}
