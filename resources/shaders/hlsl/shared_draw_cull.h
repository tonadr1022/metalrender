#ifndef SHARED_DRAW_CULL_H
#define SHARED_DRAW_CULL_H

#include "shader_core.h"

struct ViewCullSetup {
  uint view_data_buf_idx;
  uint view_data_buf_offset_bytes;
  uint cull_data_idx;
  uint cull_data_offset_bytes;
  uint task_cmd_buf_idx_opaque;
  uint task_cmd_buf_alpha_test_idx;
  uint draw_cnt_buf_idx;
  uint pad;
};

cbuffer DrawCullPC HLSL_PC_REG {
  uint view_cull_setup_buf_idx;
  uint view_cull_setup_count;
  uint view_cull_setup_buf_offset_bytes;
  uint instance_data_buf_idx;
  uint materials_buf_idx;
  uint mesh_data_buf_idx;
  uint max_draws;
  uint culling_enabled;
};


#endif
