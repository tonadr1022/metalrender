#ifndef SHARED_DRAW_CULL_H
#define SHARED_DRAW_CULL_H

#include "../shader_core.h"

cbuffer DrawCullPC HLSL_PC_REG {
  uint task_cmd_buf_idx;
  uint draw_cnt_buf_idx;
};

#endif
