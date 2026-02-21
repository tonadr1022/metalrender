#ifndef MATERIAL_H
#define MATERIAL_H

#include "shader_core.h"

#define M4MAT_FLAG_ALPHATEST 0x1

struct M4Material {
  uint albedo_tex_idx;
  uint normal_tex_idx;
  uint flags;  // bottom bit is alpha test enabled.
  uint _pad;
  float4 color;
};

#endif
