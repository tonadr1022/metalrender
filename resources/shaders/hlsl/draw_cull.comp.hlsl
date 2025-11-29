// clang-format off
#define COMPUTE_ROOT_SIG
#include "root_sig.h"
#include "shared_draw_cull.h"
#include "shared_mesh_data.h"
#include "shared_instance_data.h"
#include "shared_task_cmd.h"
#include "../shader_constants.h"
// clang-format on

[RootSignature(ROOT_SIGNATURE)][NumThreads(64, 1, 1)] void main(uint dtid : SV_DispatchThreadID) {
  if (dtid >= max_draws) {
    return;
  }
  StructuredBuffer<InstanceData> instance_data_buf = ResourceDescriptorHeap[instance_data_buf_idx];
  InstanceData instance_data = instance_data_buf[dtid];
  StructuredBuffer<MeshData> mesh_data_buf = ResourceDescriptorHeap[mesh_data_buf_idx];
  MeshData mesh_data = mesh_data_buf[instance_data.mesh_id];

  RWStructuredBuffer<TaskCmd> task_cmd_buf = ResourceDescriptorHeap[task_cmd_buf_idx];
  uint task_groups = (mesh_data.meshlet_count + K_TASK_TG_SIZE - 1) / K_TASK_TG_SIZE;

  RWStructuredBuffer<uint> task_cmd_cnt_buf = ResourceDescriptorHeap[draw_cnt_buf_idx];
  uint task_group_base_i;
  InterlockedAdd(task_cmd_cnt_buf[0], task_groups, task_group_base_i);

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
    task_cmd_buf[task_group_base_i + task_group_i].task_count =
        min(K_TASK_TG_SIZE, mesh_data.meshlet_count - task_group_i * K_TASK_TG_SIZE);
  }
}
