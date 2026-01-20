#ifndef SHARED_DRAW_CULL_H
#define SHARED_DRAW_CULL_H

#include "shader_core.h"

cbuffer DrawCullPC HLSL_PC_REG {
  uint globals_buf_idx;
  uint globals_buf_offset_bytes;
  uint cull_data_idx;
  uint cull_data_offset_bytes;
  uint task_cmd_buf_idx;
  uint draw_cnt_buf_idx;
  uint instance_data_buf_idx;
  uint mesh_data_buf_idx;
  uint max_draws;
};

#endif
