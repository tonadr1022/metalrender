// clang-format off
#include "../root_sig.hlsl"
#include "shared_depth_reduce.h"
// clang-format on

#define REVERSE_Z 1
#ifdef REVERSE_Z
#define COMP_FUNC min
#define INVALID_DEPTH 1.0
#else
#define COMP_FUNC max
#define INVALID_DEPTH -1.0
#endif

Texture2D<float> in_tex : register(t0);
RWTexture2D<float> out_tex : register(u0);

[RootSignature(ROOT_SIGNATURE)][NumThreads(8, 8, 1)] void main(uint2 dtid
                                                               : SV_DispatchThreadID) {
  if (dtid.x >= pc.out_tex_dim_x || dtid.y >= pc.out_tex_dim_y) return;

  int2 in_start = int2(dtid.x * pc.in_tex_dim_x / pc.out_tex_dim_x,
                       dtid.y * pc.in_tex_dim_y / pc.out_tex_dim_y);

  int2 in_end = int2((dtid.x + 1) * pc.in_tex_dim_x / pc.out_tex_dim_x,
                     (dtid.y + 1) * pc.in_tex_dim_y / pc.out_tex_dim_y);

  float depth = INVALID_DEPTH;

  for (int y = in_start.y; y < in_end.y; ++y) {
    for (int x = in_start.x; x < in_end.x; ++x) {
      depth = COMP_FUNC(depth, in_tex.Load(int3(x, y, 0)));
    }
  }

  out_tex[dtid] = depth;
}
