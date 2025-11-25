#ifndef SHARED_DRAW_CULL_H
#define SHARED_DRAW_CULL_H

#include "../shader_core.h"

cbuffer DrawCullPC HLSL_PC_REG {
  uint out_draw_cnt_buf_idx;
  uint test_buf_idx;
};

#endif
