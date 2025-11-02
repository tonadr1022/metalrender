#include "root_sig.h"
#include "shared_basic_tri.h"
#include "material.h"
#include "../default_vertex.h"

#include "bindless.hlsli"

struct VOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};


[RootSignature(ROOT_SIGNATURE)]
VOut vert_main(uint vert_id : SV_VertexID) {
    DefaultVertex v = BufferTable[vert_buf_idx].BUFLOAD(DefaultVertex, vert_id);
    VOut o;
    o.uv = v.uv;
    o.pos = mul(mvp, float4(v.pos.xyz, 1.0));
    return o;
}

[RootSignature(ROOT_SIGNATURE)]
float4 frag_main(VOut input) : SV_Target0 {
    M4Material material = BufferTable[mat_buf_idx].BUFLOAD(M4Material, mat_buf_id);
    float4 albedo = TextureTable[material.albedo_tex_idx].Sample(SamplerTable[0], input.uv);
//    return float4(input.uv.xy, 0.0,1.0);
    return float4(albedo.xyz * material.color.xyz, 1.0);
}
