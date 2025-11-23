#ifndef MATERIAL_H
#define MATERIAL_H

#include "../shader_core.h"

struct M4Material {
  uint albedo_tex_idx;
  float4 color;
};

#endif
