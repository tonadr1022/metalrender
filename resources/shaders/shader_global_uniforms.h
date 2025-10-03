#ifndef SHADER_GLOBAL_UNIFORMS_H
#define SHADER_GLOBAL_UNIFORMS_H

#include "shader_core.h"

struct Uniforms {
  float4x4 vp;
  float4x4 proj;
  float4x4 view;
  uint render_mode;
};

#endif
