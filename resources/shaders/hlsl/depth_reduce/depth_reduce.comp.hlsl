// clang-format off
#define COMPUTE_ROOT_SIG
#include "../root_sig.h"
#include "shared_depth_reduce.h"
// clang-format on

#define REVERSE_Z 1
#ifdef REVERSE_Z
#define COMP_FUNC min
#define INVALID_DEPTH 1.0
#else
#define COMP_FUNC max
#define INVALID_DEPTH 1.0
#endif

float read_tex(in Texture2D<float> tex, int2 texel, in SamplerState sampler) {
  if (texel.x < 0 || texel.y < 0 || texel.x >= in_tex_dim_x || texel.y >= in_tex_dim_y) {
    return INVALID_DEPTH;
  }
  return tex.SampleLevel(
      sampler, (float2(texel) + float2(0.5, 0.5)) / float2(in_tex_dim_x, in_tex_dim_y), 0);
}

[RootSignature(ROOT_SIGNATURE)][NumThreads(8, 8, 1)] void main(uint2 dtid : SV_DispatchThreadID) {
  if (dtid.x >= out_tex_dim_x || dtid.y >= out_tex_dim_y) {
    return;
  }

  Texture2D<float> in_tex = ResourceDescriptorHeap[in_tex_idx];
  RWTexture2D<float> out_tex = ResourceDescriptorHeap[out_tex_idx];
  SamplerState sampler = SamplerDescriptorHeap[NEAREST_SAMPLER_IDX];

  // Handle the case where input dim is not 2x the output dim.
  float2 in_coord = (float2(dtid) + 0.5) * float2(in_tex_dim_x, in_tex_dim_y) /
                    float2(out_tex_dim_x, out_tex_dim_y);
  int2 base = int2(floor(in_coord));

  float depth_val = read_tex(in_tex, base, sampler);
  depth_val = COMP_FUNC(depth_val, read_tex(in_tex, base + int2(0, 1), sampler));
  depth_val = COMP_FUNC(depth_val, read_tex(in_tex, base + int2(1, 0), sampler));
  depth_val = COMP_FUNC(depth_val, read_tex(in_tex, base + int2(1, 1), sampler));

  out_tex[dtid] = depth_val;
}
