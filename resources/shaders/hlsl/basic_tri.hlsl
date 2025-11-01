#define bufferSpace space1
//#define texture2DSpace space2


#include "root_sig.h"
#include "shared_basic_tri.h"
#include "material.h"
#include "../default_vertex.h"

ByteAddressBuffer BufferTable[] : register(t0, bufferSpace);
//Texture2D TextureTable[] : register(t0, texture2DSpace);


struct VOut
{
    float4 pos : SV_Position;
    float3 color : COLOR0;
    float2 uv : TEXCOORD0;
};



[RootSignature(ROOT_SIGNATURE)]
VOut vert_main(uint vert_id : SV_VertexID) {
    DefaultVertex v = BufferTable[vert_buf_idx].Load<DefaultVertex>(sizeof(DefaultVertex) * vert_id);
    VOut o;
    o.color = v.color;
//    o.color = float3(vert_id & 1, vert_id & 2, vert_id & 4);
    //o.uv = v.uv;
    o.uv = float2(0.0,0.0);
    o.pos = mul(mvp, float4(v.pos.xyz, 1.0));
    //o.pos = float4(v.pos.xyz, 1.0);
    return o;
}

[RootSignature(ROOT_SIGNATURE)]
float4 frag_main(VOut input) : SV_Target0 {
    M4Material material = BufferTable[mat_buf_idx].Load<M4Material>(sizeof(M4Material) * mat_buf_id);
    return float4(input.color + material.color.xyz, 1.0);
}
