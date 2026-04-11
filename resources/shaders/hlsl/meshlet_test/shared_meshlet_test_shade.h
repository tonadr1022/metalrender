#ifndef SHARED_MESHLET_TEST_SHADE_H
#define SHARED_MESHLET_TEST_SHADE_H

#include "../shader_core.h"

struct MeshletShadePC {
  uint gbuffer_a_idx;
  uint depth_pyramid_view_idx;
  uint swap_w;
  uint swap_h;
  uint pyramid_base_w;
  uint pyramid_base_h;
};

PUSHCONSTANT(MeshletShadePC, pc);

#endif
