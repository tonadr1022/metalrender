#ifndef __HLSL__
#define __HLSL__ 1
#endif

#define ROOT_SIGNATURE                                       \
  "CBV(b0, space = 0, visibility = SHADER_VISIBILITY_ALL), " \
  "DescriptorTable(SRV(t0, numDescriptors = 1024, space = 1), visibility = SHADER_VISIBILITY_ALL)"
//  "DescriptorTable(SRV(t0, numDescriptors = 1024, space = 2), visibility = SHADER_VISIBILITY_ALL)"
