// clang-format off
#include "root_sig.hlsl"
#include "shader_constants.h"
#include "shader_core.h"
#include "shared_forward_meshlet.h"
#include "shared_task_cmd.h"
// clang-format on

StructuredBuffer<TaskCmd> task_cmd_buf : register(t4);

groupshared Payload s_Payload;
groupshared uint s_visible_meshlet_cnt;

[NumThreads(K_TASK_TG_SIZE, 1, 1)] void main(uint gtid
                                             : SV_GroupThreadID, uint gid
                                             : SV_GroupID) {
  StructuredBuffer<uint3> draw_cnt_buf = bindless_buffers_uint3[pc.out_draw_count_buf_idx];
  uint task_group_id = gid;
  bool valid_task_group = task_group_id < draw_cnt_buf[pc.alpha_test_enabled].x;

  TaskCmd task_cmd;
  bool draw = false;
  if (valid_task_group) {
    task_cmd = task_cmd_buf[task_group_id];
    draw = (gtid < task_cmd.task_count);
  }

  if (gtid == 0) {
    s_visible_meshlet_cnt = 0;
  }
  GroupMemoryBarrierWithGroupSync();

  if (draw) {
    uint thread_i;
    InterlockedAdd(s_visible_meshlet_cnt, 1, thread_i);
    s_Payload.meshlet_indices[thread_i] = (task_group_id & 0xFFFFFFu) | (gtid << 24);
  }

  GroupMemoryBarrierWithGroupSync();

  uint visible_meshlet_cnt = s_visible_meshlet_cnt;
  DispatchMesh(visible_meshlet_cnt, 1, 1, s_Payload);
}
