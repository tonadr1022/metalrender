#include "root_sig.h"
#include "shared_imgui.h"

[RootSignature(ROOT_SIGNATURE)]
float4 main(VOut input) : SV_Target {
    Texture2D tex = ResourceDescriptorHeap[tex_idx];
    SamplerState samp = SamplerDescriptorHeap[LINEAR_SAMPLER_IDX];
    float4 c = input.color * tex.Sample(samp, input.uv);
    return c;
}
