#ifndef __HLSL__
#define __HLSL__ 1
#endif

#define ROOT_SIGNATURE                                                                         \
  "RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED), "             \
  "RootConstants(num32BitConstants = 40, b0, space = 0, visibility = SHADER_VISIBILITY_ALL), " \
  "RootConstants(num32BitConstants = 1, b1, space = 0, visibility = SHADER_VISIBILITY_ALL)"

#define NEAREST_SAMPLER_IDX 0
#define LINEAR_SAMPLER_IDX 1
