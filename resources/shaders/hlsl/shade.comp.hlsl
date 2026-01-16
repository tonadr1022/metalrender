// clang-format off
#define COMPUTE_ROOT_SIG
#include "root_sig.h"
#include "shared_shade.h"

// clang-format on

[RootSignature(ROOT_SIGNATURE)][NumThreads(8, 8, 1)] void main(uint2 dtid : SV_DispatchThreadID) {
  if (dtid.x >= img_dims.x || dtid.y >= img_dims.y) {
    return;
  }

  RWTexture2D<float4> out_tex = ResourceDescriptorHeap[output_tex_idx];
  Texture2D<float4> input_tex = ResourceDescriptorHeap[gbuffer_a_tex_idx];
  float4 color = input_tex.Load(int3(dtid, 0));
  out_tex[dtid] = color;
}
