#ifndef SHARED_DRAW_CULL_H
#define SHARED_DRAW_CULL_H

#include "shader_core.h"

cbuffer DrawCullPC HLSL_PC_REG {
  uint view_data_buf_idx;
  uint view_data_buf_offset_bytes;
  uint cull_data_idx;
  uint cull_data_offset_bytes;
  // TODO: this is cursed, maybe separate arrays for buckets of instance datas depending on this
  uint task_cmd_buf_idx_opaque;
  uint task_cmd_buf_alpha_test_idx;
  uint draw_cnt_buf_idx;
  uint draw_batch_idx;
  uint instance_data_buf_idx;
  uint materials_buf_idx;
  uint mesh_data_buf_idx;
  uint max_draws;
  uint culling_enabled;
};

#endif
