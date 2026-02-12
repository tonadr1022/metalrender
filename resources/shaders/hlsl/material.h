#ifndef MATERIAL_H
#define MATERIAL_H

#include "shader_core.h"

struct M4Material {
  uint albedo_tex_idx;
  uint normal_tex_idx;
  uint2 _pad;
  float4 color;
};

#endif
