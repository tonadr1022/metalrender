// clang-format off
#define COMPUTE_ROOT_SIG
#include "root_sig.hlsl"
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
  InstanceData instance_data =
      bindless_buffers[instance_data_buf_idx].Load<InstanceData>(dtid * sizeof(InstanceData));
  if (instance_data.mesh_id == 0xFFFFFFFF) {
    return;
  }
  MeshData mesh_data =
      bindless_buffers[mesh_data_buf_idx].Load<MeshData>(instance_data.mesh_id * sizeof(MeshData));
  ViewData view_data =
      bindless_buffers[view_data_buf_idx].Load<ViewData>(view_data_buf_offset_bytes);
  CullData cull_data = bindless_buffers[cull_data_idx].Load<CullData>(cull_data_offset_bytes);

  uint task_groups = (mesh_data.meshlet_count + K_TASK_TG_SIZE - 1) / K_TASK_TG_SIZE;

  bool visible = true;
  if (culling_enabled != 0) {
    float3 world_center =
        rotate_quat(instance_data.scale * mesh_data.center, instance_data.rotation) +
        instance_data.translation;
    float radius = mesh_data.radius * instance_data.scale;
    float4 center = mul(view_data.view, float4(world_center, 1.0));
    visible = visible && (-center.z + radius) > cull_data.z_near &&
              (-center.z - radius) < cull_data.z_far;
    visible = visible &&
              (center.z * cull_data.frustum[3] - abs(center.y) * cull_data.frustum[2]) > -radius;
    visible = visible &&
              (center.z * cull_data.frustum[1] - abs(center.x) * cull_data.frustum[0]) > -radius;
  }

  if (visible) {
    uint task_group_base_i;
    RWByteAddressBuffer task_cmd_cnt_buf = bindless_rwbuffers[draw_cnt_buf_idx];
    task_cmd_cnt_buf.InterlockedAdd(0, task_groups, task_group_base_i);

    TaskCmd cmd;
    for (uint task_group_i = 0; task_group_i < task_groups; task_group_i++) {
      cmd.instance_id = dtid;
      cmd.task_offset = mesh_data.meshlet_base + task_group_i * K_TASK_TG_SIZE;
      cmd.group_base = task_group_i * K_TASK_TG_SIZE;
      cmd.task_count = min(K_TASK_TG_SIZE, mesh_data.meshlet_count - task_group_i * K_TASK_TG_SIZE);
      bindless_rwbuffers[task_cmd_buf_idx].Store<TaskCmd>(
          (task_group_base_i + task_group_i) * sizeof(TaskCmd), cmd);
    }
  }
}
