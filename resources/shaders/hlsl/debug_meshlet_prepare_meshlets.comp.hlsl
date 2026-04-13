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

#ifndef LATE
#define LATE 0
#endif

groupshared uint g_visible_in_group;

CONSTANT_BUFFER(ViewData, view_data, VIEW_DATA_SLOT);
CONSTANT_BUFFER(CullData, cull_data, 4);

[NumThreads(64, 1, 1)] void main(uint dtid
                                 : SV_DispatchThreadID, uint gtid
                                 : SV_GroupIndex) {
  const bool in_range = dtid < pc.max_draws;

  const bool object_occlusion_enabled =
      (pc.flags & MESHLET_PREPARE_OBJECT_OCCLUSION_CULL_ENABLED_BIT) != 0;
  const bool object_frustum_cull_enabled =
      (pc.flags & MESHLET_PREPARE_OBJECT_FRUSTUM_CULL_ENABLED_BIT) != 0;

  InstanceData instance_data = (InstanceData)0;
  uint mesh_id = 0xFFFFFFFFu;
  if (in_range) {
    instance_data = bindless_buffers[pc.instance_data_buf_idx].Load<InstanceData>(
        dtid * (uint)sizeof(InstanceData));
    mesh_id = instance_data.mesh_id;
  }

  const bool valid_mesh = in_range && (mesh_id != 0xFFFFFFFFu);

  // Read per-object visibility from the previous frame.
  // Default true so early pass emits everything when occlusion is disabled.
  bool visible_last_frame = true;
  if (valid_mesh && object_occlusion_enabled) {
    RWByteAddressBuffer instance_vis_buf = bindless_rwbuffers[pc.instance_vis_buf_idx];
    visible_last_frame = instance_vis_buf.Load(dtid * (uint)sizeof(uint)) != 0;
  }

  // Early pass: only process objects that were visible last frame.
  // Late pass: process every object (finds newly visible ones + runs occlusion test).
  const bool should_process =
      valid_mesh && (LATE || !object_occlusion_enabled || visible_last_frame);

  MeshData mesh_data = (MeshData)0;
  uint task_groups = 0;
  if (should_process) {
    mesh_data =
        bindless_buffers[pc.mesh_data_buf_idx].Load<MeshData>(mesh_id * (uint)sizeof(MeshData));
    task_groups = (mesh_data.meshlet_count + K_TASK_TG_SIZE - 1) / K_TASK_TG_SIZE;
  }

  bool visible = should_process;

  // Frustum + near/far plane cull.
  if (should_process && (object_frustum_cull_enabled || object_occlusion_enabled)) {
    float3 world_center =
        rotate_quat(instance_data.scale * mesh_data.center, instance_data.rotation) +
        instance_data.translation;
    float radius = mesh_data.radius * instance_data.scale;
    float4 center = mul(view_data.view, float4(world_center, 1.0));

    if (object_frustum_cull_enabled) {
      visible = visible && ((-center.z + radius) > cull_data.z_near) &&
                ((-center.z - radius) < cull_data.z_far);
      visible = visible &&
                (center.z * cull_data.frustum[3] - abs(center.y) * cull_data.frustum[2] > -radius);
      visible = visible &&
                (center.z * cull_data.frustum[1] - abs(center.x) * cull_data.frustum[0] > -radius);
    }

    // Late pass: HZB occlusion test (mirrors draw_cull.comp.hlsl).
    if (LATE && object_occlusion_enabled && visible) {
      ProjectSphereResult proj_res =
          project_sphere(center.xyz, radius, cull_data.z_near, cull_data.p00, cull_data.p11);

      if (proj_res.success && !any(isnan(proj_res.aabb)) && !any(isinf(proj_res.aabb))) {
        float4 aabb = proj_res.aabb;
        uint2 texSize = uint2(cull_data.pyramid_width, cull_data.pyramid_height);

        float2 aabb_size = (aabb.zw - aabb.xy) * float2(texSize);
        float lod = floor(log2(max(aabb_size.x, aabb_size.y)));
        lod = clamp(lod, 0.0, float(cull_data.pyramid_mip_count) - 1.0);

        int lod_i = int(lod);
        uint2 mipDims = uint2(max(1u, cull_data.pyramid_width >> uint(lod_i)),
                              max(1u, cull_data.pyramid_height >> uint(lod_i)));

        float2 texelSize = 1.0f / float2(mipDims);
        float2 halfTexel = texelSize * 0.5f;

        float2 center_uv = (aabb.xy + aabb.zw) * 0.5f;
        center_uv = clamp(center_uv, 0.0f, 1.0f);

        Texture2D depth_pyramid_tex = bindless_textures[pc.depth_pyramid_tex_idx];
        SamplerState samp = bindless_samplers[NEAREST_CLAMP_EDGE_SAMPLER_IDX];

        float d0 = depth_pyramid_tex
                       .SampleLevel(samp, center_uv + float2(-halfTexel.x, -halfTexel.y), lod_i)
                       .x;
        float d1 = depth_pyramid_tex
                       .SampleLevel(samp, center_uv + float2(-halfTexel.x, halfTexel.y), lod_i)
                       .x;
        float d2 = depth_pyramid_tex
                       .SampleLevel(samp, center_uv + float2(halfTexel.x, -halfTexel.y), lod_i)
                       .x;
        float d3 =
            depth_pyramid_tex.SampleLevel(samp, center_uv + float2(halfTexel.x, halfTexel.y), lod_i)
                .x;

        float depth = min(min(d0, d1), min(d2, d3));

        float view_z = center.z + radius;
        float zn = cull_data.z_near;
        float depth_sphere = zn / -view_z;
        visible = visible && (depth_sphere >= depth);
      }
    }
  }

  // store per-object visibility for next frame's early pass
  if (LATE && object_occlusion_enabled && valid_mesh) {
    RWByteAddressBuffer instance_vis_buf = bindless_rwbuffers[pc.instance_vis_buf_idx];
    instance_vis_buf.Store(dtid * (uint)sizeof(uint), visible ? 1u : 0u);
  }

  // Emit decision mirrors draw_cull.comp.hlsl when meshlet occlusion is enabled:
  //   should_emit = meshlet_occlusion_enabled ? visible : (visible && !visible_last_frame)
  // Meshlet occlusion is always on in this scene, so should_emit = visible.
  // For the early pass, non-visible-last-frame objects already have visible=false via
  // should_process, so this naturally matches the early-pass skip logic.
  const bool should_emit = visible;

  // --- Groupshared visible-object counter (all threads must reach barriers) ---
  if (LATE && gtid == 0) {
    g_visible_in_group = 0;
  }
  GroupMemoryBarrierWithGroupSync();

  const uint lane_contrib = should_emit ? 1u : 0u;
  const uint wave_sum = WaveActiveSum(lane_contrib);
  if (LATE && WaveIsFirstLane()) {
    InterlockedAdd(g_visible_in_group, wave_sum);
  }
  GroupMemoryBarrierWithGroupSync();

  if (LATE && gtid == 0 && g_visible_in_group > 0) {
    RWByteAddressBuffer vis_cnt_buf = bindless_rwbuffers[pc.visible_obj_cnt_buf_idx];
    uint unused;
    vis_cnt_buf.InterlockedAdd(0, g_visible_in_group, unused);
  }
  GroupMemoryBarrierWithGroupSync();

  if (!should_emit) {
    return;
  }

  RWByteAddressBuffer task_cmd_cnt_buf = bindless_rwbuffers[pc.taskcmd_cnt_buf_idx];
  uint task_group_base_i;
  task_cmd_cnt_buf.InterlockedAdd(0, task_groups, task_group_base_i);

  TaskCmd cmd;
  cmd.instance_id = dtid;
  // Tells the task shader whether to trust the meshlet visibility buffer:
  //   1 = object was visible last frame → rely on meshlet vis buf for early occlusion
  //   0 = newly visible object → task shader draws all meshlets fresh
  cmd.late_draw_visibility = uint(visible_last_frame);
  RWByteAddressBuffer dst_buf = bindless_rwbuffers[pc.dst_task_cmd_buf_idx];
  for (uint i = 0; i < task_groups; ++i) {
    cmd.task_offset = mesh_data.meshlet_base + i * K_TASK_TG_SIZE;
    cmd.group_base = i * K_TASK_TG_SIZE;
    cmd.task_count = min(K_TASK_TG_SIZE, mesh_data.meshlet_count - i * K_TASK_TG_SIZE);
    dst_buf.Store<TaskCmd>((task_group_base_i + i) * (uint)sizeof(TaskCmd), cmd);
  }
}
