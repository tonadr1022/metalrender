#ifndef SHARED_CULL_DATA_H
#define SHARED_CULL_DATA_H

#include "shader_core.h"

struct CullData {
  float4 frustum;
  float z_near;
  float z_far;
};

#endif
