#ifndef SHARED_TEX_ONLY_H
#define SHARED_TEX_ONLY_H

#include "../shader_core.h"

cbuffer TexOnlyPC HLSL_PC_REG {
  uint tex_idx;
  uint mip_level;
};
#endif
