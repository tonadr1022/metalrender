// clang-format off
#include "root_sig.h"
#include "../shader_constants.h"
#include "../shader_core.h"
#include "shared_test_task.h"
#include "shared_mesh_data.h"
// clang-format off

groupshared Payload s_Payload;

[RootSignature(ROOT_SIGNATURE)]
[NumThreads(K_TASK_TG_SIZE, 1, 1)]
void main(uint gtid : SV_GroupThreadID, uint dtid : SV_DispatchThreadID, uint gid : SV_GroupID) {
    StructuredBuffer<MeshData> task_cmds = ResourceDescriptorHeap[task_cmd_buf_idx];
    MeshData task_cmd = task_cmds[task_cmd_idx];

    bool visible = false;

    if (dtid < task_cmd.meshlet_count) {
        visible = true;
    }

    if (visible) {
        uint index = WavePrefixCountBits(visible);
        s_Payload.meshlet_indices[index] = dtid;
    }

    uint visible_count = WaveActiveCountBits(visible);

    DispatchMesh(visible_count, 1, 1, s_Payload);
}
