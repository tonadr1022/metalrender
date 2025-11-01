#ifndef MATERIAL_H
#define MATERIAL_H

#include "../shader_core.h"

struct M4Material {
  float4 color;
  uint albedo_tex_idx;
  uint3 _pad;
};

#endif
