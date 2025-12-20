// clang-format off
#define COMPUTE_ROOT_SIG
#include "root_sig.h"
#include "../shader_constants.h"
#include "../shader_core.h"
#include "shared_task2.h"
#include "shared_mesh_data.h"
#include "math.hlsli"
#include "shared_task_cmd.h"
#include "shared_instance_data.h"
#include "shared_globals.h"
#include "shared_cull_data.h"
#include "../default_vertex.h"
// clang-format off

groupshared Payload s_Payload;
groupshared uint s_count; 

[RootSignature(ROOT_SIGNATURE)]
[NumThreads(K_TASK_TG_SIZE, 1, 1)]
void main(uint gtid : SV_GroupThreadID, uint dtid : SV_DispatchThreadID, uint gid : SV_GroupID) {
    uint task_group_id = gid;

    bool visible = false;
    
    StructuredBuffer<TaskCmd> tts =  ResourceDescriptorHeap[tt_cmd_buf_idx];
    StructuredBuffer<uint3> task_dispatch_buf = ResourceDescriptorHeap[draw_cnt_buf_idx];

    if (task_group_id < task_dispatch_buf[0].x) {
      TaskCmd tt = tts[task_group_id];
      if (gtid < tt.task_count) {
        visible = true;
      
        ByteAddressBuffer global_data_buf = ResourceDescriptorHeap[globals_buf.idx];
        GlobalData globals = global_data_buf.Load<GlobalData>(globals_buf.offset_bytes);
        StructuredBuffer<Meshlet> meshlet_buf = ResourceDescriptorHeap[meshlet_buf_idx];
        Meshlet meshlet = meshlet_buf[tt.task_offset + gtid];
        StructuredBuffer<InstanceData> instance_data_buf = ResourceDescriptorHeap[instance_data_buf_idx];
        InstanceData instance_data = instance_data_buf[tt.instance_id];
        ByteAddressBuffer cull_data_buf = ResourceDescriptorHeap[globals_buf.idx];
        CullData cull_data = cull_data_buf.Load<CullData>(sizeof(GlobalData));
        float3 world_center = rotate_quat(instance_data.scale * meshlet.center_radius.xyz, instance_data.rotation)
                              + instance_data.translation;
        float radius = meshlet.center_radius.w * instance_data.scale;
        float4 center = mul(globals.view, float4(world_center, 1.0));
        // Ref: https://github.com/zeux/niagara/blob/master/src/shaders/clustercull.comp.glsl#L101C1-L102C102
        // frustum cull, plane symmetry 
        visible = visible && (-center.z + radius) > cull_data.z_near && (-center.z - radius) < cull_data.z_far;
        visible = visible && (center.z * cull_data.frustum[3] - abs(center.y) * cull_data.frustum[2]) > -radius;
        visible = visible && (center.z * cull_data.frustum[1] - abs(center.x) * cull_data.frustum[0]) > -radius;
      }
    }

    if (gtid == 0) {
      s_count = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    if (visible) {
        uint thread_i;
        InterlockedAdd(s_count, 1, thread_i);
        s_Payload.meshlet_indices[thread_i] = (task_group_id & 0xFFFFFFu) | (gtid << 24);
    }

    GroupMemoryBarrierWithGroupSync();

    uint visible_count = s_count;

    DispatchMesh(visible_count, 1, 1, s_Payload);
}
