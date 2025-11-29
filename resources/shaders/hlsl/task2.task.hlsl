// clang-format off
#define COMPUTE_ROOT_SIG
#include "root_sig.h"
#include "../shader_constants.h"
#include "../shader_core.h"
#include "shared_task2.h"
#include "shared_mesh_data.h"
#include "shared_task_cmd.h"
// clang-format off

groupshared Payload s_Payload;
groupshared uint s_count; 

[RootSignature(ROOT_SIGNATURE)]
[NumThreads(K_TASK_TG_SIZE, 1, 1)]
void main(uint gtid : SV_GroupThreadID, uint dtid : SV_DispatchThreadID, uint gid : SV_GroupID) {
    uint task_group_id = gid;

    bool visible = false;
    
    StructuredBuffer<TaskCmd> tts =  ResourceDescriptorHeap[tt_cmd_buf_idx];
    StructuredBuffer<uint3> abc = ResourceDescriptorHeap[draw_cnt_buf_idx];

    TaskCmd tt = tts[task_group_id];
    if (gtid < tt.task_count && abc[0].x <= 147) {
      visible = true;
    }

    if (gtid == 0) {
      s_count = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    if (visible) {
        uint thread_i;
        InterlockedAdd(s_count, 1, thread_i);
        s_Payload.meshlet_indices[thread_i] = (gid & 0xFFFFFFu) | (gtid << 24);
    }

    GroupMemoryBarrierWithGroupSync();

    uint visible_count = s_count;

    DispatchMesh(visible_count, 1, 1, s_Payload);
}
