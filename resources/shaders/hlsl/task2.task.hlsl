// clang-format off
#include "root_sig.h"
#include "../shader_constants.h"
#include "../shader_core.h"
#include "shared_task2.h"
#include "shared_mesh_data.h"
#include "shared_task_cmd.h"
// clang-format off

groupshared Payload s_Payload;

[RootSignature(ROOT_SIGNATURE)]
[NumThreads(K_TASK_TG_SIZE, 1, 1)]
void main(uint gtid : SV_GroupThreadID, uint dtid : SV_DispatchThreadID, uint gid : SV_GroupID) {
    uint task_group_id = gid;
    bool visible = false;
    
    StructuredBuffer<TaskCmd> tts =  ResourceDescriptorHeap[tt_cmd_buf_idx];

    TaskCmd tt = tts[task_group_id];
    uint meshlet_idx = tt.task_offset + gtid;
    if (gtid < tt.task_count) {
      visible = true;
      if (visible) {
          uint index = WavePrefixCountBits(visible);
          s_Payload.meshlet_indices[index] = task_group_id + (gtid << 24);
      }
    }

    uint visible_count = WaveActiveCountBits(visible);

    DispatchMesh(visible_count, 1, 1, s_Payload);
}
