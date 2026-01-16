#include "root_sig.h"
#include "shared_tex_only.h"

struct VOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

[RootSignature(ROOT_SIGNATURE)] float4 main(VOut input) : SV_Target {
  Texture2D tex = ResourceDescriptorHeap[tex_idx];
  SamplerState samp = SamplerDescriptorHeap[NEAREST_SAMPLER_IDX];
  return tex.SampleLevel(samp, input.uv, mip_level);
}
