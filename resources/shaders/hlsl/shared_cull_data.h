#ifndef SHARED_CULL_DATA_H
#define SHARED_CULL_DATA_H

#include "shader_core.h"

struct CullData {
  float4 frustum;
  float z_near;
  float z_far;
  float p00;
  float p11;
  uint pyramid_width;
  uint pyramid_height;
  uint pyramid_mip_count;
  uint paused;
};

#endif
