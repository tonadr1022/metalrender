#ifndef SHARED_GLOBALS_H
#define SHARED_GLOBALS_H

#include "../shader_core.h"

struct GlobalData {
  float4x4 vp;
  float4x4 view;
  float4x4 proj;
  float4 camera_pos;
};

#endif
