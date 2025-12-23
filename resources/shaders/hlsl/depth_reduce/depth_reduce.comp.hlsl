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
  if (texel.x < 0 || texel.y < 0 || texel.x >= int(in_tex_dims.x) ||
      texel.x >= int(in_tex_dims.y)) {
    return INVALID_DEPTH;
  }
  return tex.Load(texel).r;
}

[RootSignature(ROOT_SIGNATURE)][NumThreads(8, 8, 1)] void main(uint2 dtid : SV_DispatchThreadID) {
  if (dtid.x >= in_tex_dims.x || dtid.y >= in_tex_dims.y) {
    return;
  }
  Texture2D<float> in_tex = ResourceDescriptorHeap[in_tex_idx];
  float min_val = read_tex(in_tex, int3(dtid.x, dtid.y, in_tex_mip_level));
  min_val = COMP_FUNC(min_val, read_tex(in_tex, int3(dtid.x, dtid.y + 1, in_tex_mip_level)));
  min_val = COMP_FUNC(min_val, read_tex(in_tex, int3(dtid.x + 1, dtid.y, in_tex_mip_level)));
  min_val = COMP_FUNC(min_val, read_tex(in_tex, int3(dtid.x + 1, dtid.y + 1, in_tex_mip_level)));

  RWTexture2D<float> out_tex = ResourceDescriptorHeap[out_tex_idx];
  out_tex[dtid] = min_val;
}
