#ifndef __HLSL__
#define __HLSL__ 1
#endif

#define ROOT_SIGNATURE                                                            \
  "RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED)," \
  "CBV(b0, space = 0, visibility = SHADER_VISIBILITY_ALL), "                      \
  "CBV(b1, space = 0, visibility = SHADER_VISIBILITY_ALL) "\
