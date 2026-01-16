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

float read_tex(in Texture2D<float> tex, int3 texel) {
  if (texel.x < 0 || texel.y < 0 || texel.x >= int(out_tex_dim_x) ||
      texel.y >= int(out_tex_dim_y)) {
    return INVALID_DEPTH;
  }
  return tex.Load(texel).r;
}

[RootSignature(ROOT_SIGNATURE)][NumThreads(8, 8, 1)] void main(uint2 dtid : SV_DispatchThreadID) {
  if (dtid.x >= out_tex_dim_x || dtid.y >= out_tex_dim_y) {
    return;
  }
  Texture2D<float> in_tex = ResourceDescriptorHeap[in_tex_idx];
  RWTexture2D<float> out_tex = ResourceDescriptorHeap[out_tex_idx];

  uint2 in_base = dtid * 2;

  float depth_val = read_tex(in_tex, int3(in_base.x, in_base.y, 0));
  depth_val = COMP_FUNC(depth_val, read_tex(in_tex, int3(in_base.x + 1, in_base.y, 0)));
  depth_val = COMP_FUNC(depth_val, read_tex(in_tex, int3(in_base.x, in_base.y + 1, 0)));
  depth_val = COMP_FUNC(depth_val, read_tex(in_tex, int3(in_base.x + 1, in_base.y + 1, 0)));

  out_tex[dtid] = depth_val * 0.1;
}
