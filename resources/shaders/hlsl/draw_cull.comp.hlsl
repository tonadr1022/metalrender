// clang-format off
#define COMPUTE_ROOT_SIG
#include "root_sig.h"
#include "shared_draw_cull.h"
#include "shared_mesh_data.h"
#include "shared_instance_data.h"
#include "shared_task_cmd.h"
#include "shader_constants.h"
#include "math.hlsli"
#include "shared_cull_data.h"
#include "shared_globals.h"
// clang-format on

struct DispatchIndirectCmd {
  uint tg_x;
  uint tg_y;
  uint tg_z;
};

[RootSignature(ROOT_SIGNATURE)][NumThreads(64, 1, 1)] void main(uint dtid : SV_DispatchThreadID) {
  if (dtid >= max_draws) {
    return;
  }
  StructuredBuffer<InstanceData> instance_data_buf = ResourceDescriptorHeap[instance_data_buf_idx];
  InstanceData instance_data = instance_data_buf[dtid];
  StructuredBuffer<MeshData> mesh_data_buf = ResourceDescriptorHeap[mesh_data_buf_idx];
  MeshData mesh_data = mesh_data_buf[instance_data.mesh_id];

  ByteAddressBuffer global_data_buf = ResourceDescriptorHeap[globals_buf_idx];
  GlobalData globals = global_data_buf.Load<GlobalData>(globals_buf_offset_bytes);
  ByteAddressBuffer cull_data_buf = ResourceDescriptorHeap[cull_data_idx];
  CullData cull_data = cull_data_buf.Load<CullData>(sizeof(GlobalData));

  RWStructuredBuffer<TaskCmd> task_cmd_buf = ResourceDescriptorHeap[task_cmd_buf_idx];
  uint task_groups = (mesh_data.meshlet_count + K_TASK_TG_SIZE - 1) / K_TASK_TG_SIZE;

  RWStructuredBuffer<DispatchIndirectCmd> task_cmd_cnt_buf =
      ResourceDescriptorHeap[draw_cnt_buf_idx];

  bool visible = true;
  float3 world_center =
      rotate_quat(instance_data.scale * mesh_data.center, instance_data.rotation) +
      instance_data.translation;
  float radius = mesh_data.radius * instance_data.scale;
  float4 center = mul(globals.view, float4(world_center, 1.0));
  visible =
      visible && (-center.z + radius) > cull_data.z_near && (-center.z - radius) < cull_data.z_far;
  visible =
      visible && (center.z * cull_data.frustum[3] - abs(center.y) * cull_data.frustum[2]) > -radius;
  visible =
      visible && (center.z * cull_data.frustum[1] - abs(center.x) * cull_data.frustum[0]) > -radius;

  if (visible) {
    uint task_group_base_i;
    InterlockedAdd(task_cmd_cnt_buf[0].tg_x, task_groups, task_group_base_i);

    uint task_cmd_buf_len;
    uint stride;
    task_cmd_buf.GetDimensions(task_cmd_buf_len, stride);
    if (task_group_base_i + task_groups >= task_cmd_buf_len) {
      return;
    }

    for (uint task_group_i = 0; task_group_i < task_groups; task_group_i++) {
      task_cmd_buf[task_group_base_i + task_group_i].instance_id = dtid;
      task_cmd_buf[task_group_base_i + task_group_i].task_offset =
          mesh_data.meshlet_base + task_group_i * K_TASK_TG_SIZE;
      task_cmd_buf[task_group_base_i + task_group_i].group_base = task_group_i * K_TASK_TG_SIZE;
      task_cmd_buf[task_group_base_i + task_group_i].task_count =
          min(K_TASK_TG_SIZE, mesh_data.meshlet_count - task_group_i * K_TASK_TG_SIZE);
    }
  }
}
