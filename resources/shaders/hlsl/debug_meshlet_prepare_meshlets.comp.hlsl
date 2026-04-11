// clang-format off
#define COMPUTE_ROOT_SIG
#include "root_sig.hlsl"
#include "shared_debug_meshlet_prepare.h"
#include "shared_task_cmd.h"
#include "shared_instance_data.h"
#include "shared_mesh_data.h"
#include "shader_constants.h"
#include "shared_globals.h"
#include "shared_cull_data.h"
#include "math.hlsli"
// clang-format on

groupshared uint g_visible_in_group;

CONSTANT_BUFFER(ViewData, view_data, VIEW_DATA_SLOT);

[NumThreads(64, 1, 1)] void main(uint dtid
                                 : SV_DispatchThreadID, uint gtid
                                 : SV_GroupIndex) {
  const bool in_range = dtid < pc.max_draws;

  InstanceData instance_data = (InstanceData)0;
  uint mesh_id = 0xFFFFFFFFu;
  if (in_range) {
    instance_data = bindless_buffers[pc.instance_data_buf_idx].Load<InstanceData>(
        dtid * (uint)sizeof(InstanceData));
    mesh_id = instance_data.mesh_id;
  }

  const bool valid_mesh = in_range && (mesh_id != 0xFFFFFFFFu);

  MeshData mesh_data = (MeshData)0;
  uint task_groups = 0;
  if (valid_mesh) {
    mesh_data =
        bindless_buffers[pc.mesh_data_buf_idx].Load<MeshData>(mesh_id * (uint)sizeof(MeshData));
    task_groups = (mesh_data.meshlet_count + K_TASK_TG_SIZE - 1) / K_TASK_TG_SIZE;
  }

  bool visible = valid_mesh;
  if (valid_mesh && pc.culling_enabled != 0) {
    CullData cull_data =
        bindless_buffers[pc.cull_data_buf_idx].Load<CullData>(pc.cull_data_offset_bytes);

    float3 world_center =
        rotate_quat(instance_data.scale * mesh_data.center, instance_data.rotation) +
        instance_data.translation;
    float radius = mesh_data.radius * instance_data.scale;
    float4 center = mul(view_data.view, float4(world_center, 1.0));

    visible = visible && ((-center.z + radius) > cull_data.z_near) &&
              ((-center.z - radius) < cull_data.z_far);
    visible = visible &&
              (center.z * cull_data.frustum[3] - abs(center.y) * cull_data.frustum[2] > -radius);
    visible = visible &&
              (center.z * cull_data.frustum[1] - abs(center.x) * cull_data.frustum[0] > -radius);
  }

  const bool contribute = visible;

  if (gtid == 0) {
    g_visible_in_group = 0;
  }
  GroupMemoryBarrierWithGroupSync();

  const uint lane_contrib = contribute ? 1u : 0u;
  const uint wave_sum = WaveActiveSum(lane_contrib);
  if (WaveIsFirstLane()) {
    InterlockedAdd(g_visible_in_group, wave_sum);
  }
  GroupMemoryBarrierWithGroupSync();

  if (gtid == 0 && g_visible_in_group > 0) {
    RWByteAddressBuffer vis_cnt_buf = bindless_rwbuffers[pc.visible_obj_cnt_buf_idx];
    uint unused;
    vis_cnt_buf.InterlockedAdd(0, g_visible_in_group, unused);
  }
  GroupMemoryBarrierWithGroupSync();

  if (!contribute) {
    return;
  }

  RWByteAddressBuffer task_cmd_cnt_buf = bindless_rwbuffers[pc.draw_cnt_buf_idx];
  uint task_group_base_i;
  task_cmd_cnt_buf.InterlockedAdd(0, task_groups, task_group_base_i);

  TaskCmd cmd;
  cmd.instance_id = dtid;
  cmd.late_draw_visibility = 1u;
  RWByteAddressBuffer dst_buf = bindless_rwbuffers[pc.dst_task_cmd_buf_idx];
  for (uint i = 0; i < task_groups; ++i) {
    cmd.task_offset = mesh_data.meshlet_base + i * K_TASK_TG_SIZE;
    cmd.group_base = i * K_TASK_TG_SIZE;
    cmd.task_count = min(K_TASK_TG_SIZE, mesh_data.meshlet_count - i * K_TASK_TG_SIZE);
    dst_buf.Store<TaskCmd>((task_group_base_i + i) * (uint)sizeof(TaskCmd), cmd);
  }
}
