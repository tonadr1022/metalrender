// clang-format off
#define COMPUTE_ROOT_SIG
#include "root_sig.h"
#include "shared_draw_cull.h"
#include "shared_mesh_data.h"
#include "shared_instance_data.h"
#include "../shader_constants.h"
// clang-format on

struct MeshDraw {
  uint idx;
  uint dtid;
};

struct TaskCmd {
  uint instance_id;
  uint task_offset;
  uint task_count;
};

// TODO: evaluate threads per threadgroup
[RootSignature(ROOT_SIGNATURE)][NumThreads(64, 1, 1)] void main(uint dtid : SV_DispatchThreadID) {
  // TODO: clear the task cmd count buf before hand
  StructuredBuffer<InstanceData> instance_data_buf = ResourceDescriptorHeap[dtid];
  InstanceData instance_data = instance_data_buf[dtid];
  StructuredBuffer<MeshData> mesh_data_buf = ResourceDescriptorHeap[instance_data.mesh_id];
  MeshData mesh_data = mesh_data_buf[instance_data.mesh_id];

  RWStructuredBuffer<TaskCmd> task_cmd_buf = ResourceDescriptorHeap[task_cmd_buf_idx];
  RWStructuredBuffer<uint> task_cmd_cnt_buf = ResourceDescriptorHeap[draw_cnt_buf_idx];
  uint task_groups = (task_idx + K_TASK_TG_SIZE - 1) / K_TASK_TG_SIZE;
  uint task_group_base_i;
  InterlockedAdd(task_cmd_cnt_buf[0], task_groups, task_group_base_i);
  for (int task_group_i = 0; task_group_i < mesh_data.meshlet_count; task_group_i++) {
    task_cmd_buf[task_group_base_i + task_group_i].instance_id = dtid;
    task_cmd_buf[task_group_base_i + task_group_i].task_offset =
        task_group_base_i + task_group_i * K_TASK_TG_SIZE;
    task_cmd_buf[task_group_base_i + task_group_i].task_count =
        min(K_TASK_TG_SIZE, mesh_data.meshlet_count - task_group_i * K_TASK_TG_SIZE);
  }
}
