#ifndef SHARED_CSM_H
#define SHARED_CSM_H

#include "shader_core.h"

#define CSM_MAX_CASCADES 4

struct CSMData {
  float4x4 light_vp_matrices[CSM_MAX_CASCADES];
  float4 biases;  // min = x, max = y, pcf scale, z, z_far: w
  float4 cascade_levels;
  uint num_cascades;
};

#endif