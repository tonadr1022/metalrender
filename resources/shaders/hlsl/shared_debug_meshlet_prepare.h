#ifndef SHARED_DEBUG_MESHLET_PREPARE_H
#define SHARED_DEBUG_MESHLET_PREPARE_H

#include "shader_core.h"

struct DebugMeshletPreparePC {
  uint dst_task_cmd_buf_idx;
  uint draw_cnt_buf_idx;
  uint instance_data_buf_idx;
  uint mesh_data_buf_idx;
  uint max_draws;
  uint culling_enabled;
  uint visible_obj_cnt_buf_idx;
  uint pass;                   // 0 = early, 1 = late
  uint instance_vis_buf_idx;   // UINT32_MAX = object occlusion disabled
  uint depth_pyramid_tex_idx;  // UINT32_MAX = disabled (late pass occlusion test only)
};

PUSHCONSTANT(DebugMeshletPreparePC, pc);

#endif
