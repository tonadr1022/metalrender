#define bufferSpace space1

#include "root_sig.hlsl"
#include "material.h"

ByteAddressBuffer BufferTable[] : register(t0, bufferSpace);

struct Vertex {
    float3 position;
    float2 uv;
};

struct VOut
{
    float4 position : SV_Position;
    float3 color : COLOR0;
    float2 uv : TEXCOORD0;
};

cbuffer ColorBuffer : register(b0) {
    uint vert_buf_idx;
    uint mat_buf_idx;
    uint mat_buf_id;
};


[RootSignature(ROOT_SIGNATURE)]
VOut vert_main(uint vert_id : SV_VertexID) {
    Vertex v = BufferTable[vert_buf_idx].Load<Vertex>(sizeof(Vertex) * vert_id);
    VOut o;
    o.color = float3(vert_id & 1, vert_id & 2, vert_id & 4);
    o.uv = v.uv;
    o.position = float4(v.position.xyz, 1.0);
    return o;
}

[RootSignature(ROOT_SIGNATURE)]
float4 frag_main(VOut input) : SV_Target0 {
    M4Material material = BufferTable[mat_buf_idx].Load<M4Material>(sizeof(M4Material) * mat_buf_id);
    return float4(input.color * material.color.xyz, 1.0);
}
