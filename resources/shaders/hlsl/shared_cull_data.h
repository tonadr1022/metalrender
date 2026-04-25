#ifndef SHARED_CULL_DATA_H
#define SHARED_CULL_DATA_H

#include "shader_core.h"

#define CULL_PROJECTION_PERSPECTIVE 0
#define CULL_PROJECTION_ORTHOGRAPHIC 1

struct CullData {
  float4 frustum;
  float4 ortho_bounds;
  float z_near;
  float z_far;
  float p00;
  float p11;
  uint pyramid_width;
  uint pyramid_height;
  uint pyramid_mip_count;
  uint paused;
  uint projection_type;
  uint _padding0;
  uint _padding1;
  uint _padding2;
};

#endif
