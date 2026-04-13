#ifndef SHARED_DEBUG_MESHLET_PREPARE_H
#define SHARED_DEBUG_MESHLET_PREPARE_H

#include "shader_core.h"

#define MESHLET_PREPARE_OBJECT_FRUSTUM_CULL_ENABLED_BIT (1 << 0)
#define MESHLET_PREPARE_OBJECT_OCCLUSION_CULL_ENABLED_BIT (1 << 1)

struct DebugMeshletPreparePC {
  uint dst_task_cmd_buf_idx;
  uint taskcmd_cnt_buf_idx;
  uint instance_data_buf_idx;
  uint mesh_data_buf_idx;
  uint max_draws;
  uint flags;
  uint visible_obj_cnt_buf_idx;
  uint instance_vis_buf_idx;
  uint depth_pyramid_tex_idx;
};

PUSHCONSTANT(DebugMeshletPreparePC, pc);

#endif
