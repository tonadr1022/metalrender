#ifndef SHARED_CSM_H
#define SHARED_CSM_H

#include "shader_core.h"

struct CSMData {
  float4x4 light_vp_matrices[4];
  float4x4 light_proj_matrices[4];
  float levels[3];
};

#endif