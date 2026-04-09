// clang-format off
#define COMPUTE_ROOT_SIG
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
  TaskCmd task_cmd = task_cmd_buf[gid];
  bool draw = (gtid < task_cmd.task_count);

  if (gtid == 0) {
    s_visible_meshlet_cnt = 0;
  }
  GroupMemoryBarrierWithGroupSync();

  if (draw) {
    uint thread_i;
    InterlockedAdd(s_visible_meshlet_cnt, 1, thread_i);
    s_Payload.meshlet_indices[thread_i] = (gid & 0xFFFFFFu) | (gtid << 24);
  }

  GroupMemoryBarrierWithGroupSync();

  uint visible_meshlet_cnt = s_visible_meshlet_cnt;
  DispatchMesh(visible_meshlet_cnt, 1, 1, s_Payload);
}
