#include "root_sig.h"
#include "bindless.hlsli"
#include "shared_imgui.h"

struct VOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    //uint8_t4_packed color : COLOR0;
};


VOut vert_main(uint vertex_id : SV_VertexID) {
    VOut o;
    float4 pos = float4(0.0,0.0,0.0,0.0);
    o.pos = mul(proj, float4(pos.xy, 0, 1));
    return o;
}

float4 frag_main(VOut input) : SV_Target {
    return float4(1.0,1.0,1.0,1.0); 
}


