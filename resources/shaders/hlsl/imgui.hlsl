#include "root_sig.h"
#include "shared_imgui.h"

struct VOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct DrawID {
    uint did;
    uint vert_id;
};

ConstantBuffer<DrawID> gDrawID : register(b1);

float4 ConvertUint32RGBAtoFloat4(uint c) {
    float r = (c        & 0xFF) / 255.0;
    float g = ((c >> 8) & 0xFF) / 255.0;
    float b = ((c >> 16) & 0xFF) / 255.0;
    float a = ((c >> 24) & 0xFF) / 255.0;
    return float4(r, g, b, a);
}

[RootSignature(ROOT_SIGNATURE)]
VOut vert_main(uint vert_id : SV_VertexID) {
    VOut o;
    StructuredBuffer<ImGuiVertex> vert_buf = ResourceDescriptorHeap[vert_buf_idx];
    ImGuiVertex vert = vert_buf[vert_id + gDrawID.vert_id];
    o.pos = mul(proj, float4(vert.position, 0.0, 1.0));
    o.uv = vert.tex_coords;
    o.color = ConvertUint32RGBAtoFloat4(vert.color);
    return o;
}

[RootSignature(ROOT_SIGNATURE)]
float4 frag_main(VOut input) : SV_Target {
    Texture2D tex = ResourceDescriptorHeap[tex_idx];
    SamplerState samp = SamplerDescriptorHeap[LINEAR_SAMPLER_IDX];
    float4 c = input.color * tex.Sample(samp, input.uv);
    return c;
}
