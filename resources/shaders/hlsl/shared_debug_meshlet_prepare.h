#ifndef SHARED_DEBUG_MESHLET_PREPARE_H
#define SHARED_DEBUG_MESHLET_PREPARE_H

#include "shader_core.h"

struct DebugMeshletPreparePC {
  uint dst_task_cmd_buf_idx;
  uint draw_cnt_buf_idx;
  uint instance_data_buf_idx;
  uint mesh_data_buf_idx;
  uint view_data_buf_idx;
  uint view_data_offset_bytes;
  uint cull_data_buf_idx;
  uint cull_data_offset_bytes;
  uint max_draws;
  uint culling_enabled;
  uint visible_obj_cnt_buf_idx;
};

PUSHCONSTANT(DebugMeshletPreparePC, pc);

#endif
