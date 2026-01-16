// clang-format off
#define COMPUTE_ROOT_SIG
#include "../root_sig.h"
#include "shared_depth_reduce.h"
// clang-format on

#ifdef REVERSE_Z
#define COMP_FUNC min
#define INVALID_DEPTH 1.0
#else
#define COMP_FUNC max
#define INVALID_DEPTH 1.0
#endif

float read_tex(in Texture2D<float> tex, float2 loc, SamplerState sampler) {
  return tex.SampleLevel(sampler, loc, 0);
}

[RootSignature(ROOT_SIGNATURE)][NumThreads(8, 8, 1)] void main(uint2 dtid : SV_DispatchThreadID) {
  if (dtid.x >= out_tex_dim_x || dtid.y >= out_tex_dim_y) {
    return;
  }
  Texture2D<float> in_tex = ResourceDescriptorHeap[in_tex_idx];
  RWTexture2D<float> out_tex = ResourceDescriptorHeap[out_tex_idx];
  SamplerState sampler = SamplerDescriptorHeap[NEAREST_SAMPLER_IDX];

  float2 in_dims = float2(in_tex_dim_x, in_tex_dim_y);
  float depth_val = in_tex.SampleLevel(sampler, in_dims / in_dims, 0);
  depth_val = in_tex.SampleLevel(sampler, (in_dims + float2(0.5, 0.0)) / in_dims, 0);
  depth_val = in_tex.SampleLevel(sampler, (in_dims + float2(0.0, 0.5)) / in_dims, 0);
  depth_val = in_tex.SampleLevel(sampler, (in_dims + float2(0.5, 0.5)) / in_dims, 0);

  out_tex[dtid] = depth_val;
}
