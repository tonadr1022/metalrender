// clang-format off
#define COMPUTE_ROOT_SIG
#include "root_sig.hlsl"
#include "shared_draw_cull.h"
#include "shared_mesh_data.h"
#include "shared_instance_data.h"
#include "material.h"
#include "shared_task_cmd.h"
#include "shader_constants.h"
#include "math.hlsli"
#include "shared_cull_data.h"
#include "shared_globals.h"
// clang-format on

struct DispatchMeshCmd {
  uint tg_x;
  uint tg_y;
  uint tg_z;
};

[NumThreads(64, 1, 1)] void main(uint dtid : SV_DispatchThreadID) {
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
  M4Material material = bindless_buffers[materials_buf_idx].Load<M4Material>(instance_data.mat_id *
                                                                             sizeof(M4Material));
  bool alpha_test_enabled = (material.flags & M4MAT_FLAG_ALPHATEST) != 0;

  uint task_groups = (mesh_data.meshlet_count + K_TASK_TG_SIZE - 1) / K_TASK_TG_SIZE;

  for (uint v = 0; v < view_cull_setup_count; v++) {
    ViewCullSetup view_setup = bindless_buffers[view_cull_setup_buf_idx].Load<ViewCullSetup>(
        view_cull_setup_buf_offset_bytes + v * sizeof(ViewCullSetup));
    ViewData view_data = bindless_buffers[view_setup.view_data_buf_idx].Load<ViewData>(
        view_setup.view_data_buf_offset_bytes);
    CullData cull_data = bindless_buffers[view_setup.cull_data_idx].Load<CullData>(
        view_setup.cull_data_offset_bytes);
    uint task_cmd_buf_idx = alpha_test_enabled ? view_setup.task_cmd_buf_alpha_test_idx
                                               : view_setup.task_cmd_buf_idx_opaque;
    Texture2D depth_pyramid_tex = bindless_textures[view_setup.depth_pyramid_tex_idx];
    SamplerState samp = bindless_samplers[NEAREST_CLAMP_EDGE_SAMPLER_IDX];
    RWByteAddressBuffer instance_vis_buf = bindless_rwbuffers[view_setup.instance_vis_buf_idx];
    uint vis_byte_addr = dtid * sizeof(uint32_t);
    bool visible_last_frame = instance_vis_buf.Load(vis_byte_addr) != 0;

    bool late = view_setup.pass != 0;
    if (!late && !visible_last_frame) {
      continue;
    }
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
      if (late && visible && (view_setup.flags & OBJECT_OCCLUSION_CULL_ENABLED_BIT) != 0) {
        // occlusion culling for late pass
        ProjectSphereResult proj_res =
            project_sphere(center.xyz, radius, cull_data.z_near, cull_data.p00, cull_data.p11);
        if (proj_res.success && !(any(isnan(proj_res.aabb)) || any(isinf(proj_res.aabb)))) {
          float4 aabb = proj_res.aabb;
          const uint2 texSize = uint2(cull_data.pyramid_width, cull_data.pyramid_height);
          float2 aabb_dims_tex_space = (aabb.zw - aabb.xy) * float2(texSize);
          float lod = floor(log2(max(aabb_dims_tex_space.x, aabb_dims_tex_space.y)));
          lod = clamp(lod, 0.0, float(cull_data.pyramid_mip_count) - 1.0);
          uint2 mipDims = uint2(max(1u, cull_data.pyramid_width >> uint(lod)),
                                max(1u, cull_data.pyramid_height >> uint(lod)));
          float2 texelSizeAtLod = float2(1.f, 1.f) / float2(mipDims);
          float2 halfTexel = texelSizeAtLod * 0.5f;
          float2 smid = (aabb.xy + aabb.zw) * 0.5f;
          smid = clamp(smid, float2(0.f, 0.f), float2(1.f, 1.f));
          int lod_i = int(lod);
          float d0 =
              depth_pyramid_tex.SampleLevel(samp, smid + float2(-halfTexel.x, -halfTexel.y), lod_i)
                  .x;
          float d1 =
              depth_pyramid_tex.SampleLevel(samp, smid + float2(-halfTexel.x, halfTexel.y), lod_i)
                  .x;
          float d2 =
              depth_pyramid_tex.SampleLevel(samp, smid + float2(halfTexel.x, -halfTexel.y), lod_i)
                  .x;
          float d3 =
              depth_pyramid_tex.SampleLevel(samp, smid + float2(halfTexel.x, halfTexel.y), lod_i).x;
          float depth = min(min(d0, d1), min(d2, d3));

          float view_z = center.z + radius;
          float near = cull_data.z_near;
          float depth_sphere = near / -view_z;
          visible = visible && depth_sphere >= depth;
        }
      }
    }

    RWByteAddressBuffer draw_cmd_count_buf = bindless_rwbuffers[view_setup.draw_cmd_count_buf_idx];
    draw_cmd_count_buf.InterlockedAdd(sizeof(uint32_t) * 2 * view_setup.pass, 1);

    if (visible) {
      uint task_group_base_i;
      RWByteAddressBuffer task_cmd_cnt_buf = bindless_rwbuffers[view_setup.draw_cnt_buf_idx];
      task_cmd_cnt_buf.InterlockedAdd(uint(alpha_test_enabled) * sizeof(DispatchMeshCmd),
                                      task_groups, task_group_base_i);

      TaskCmd cmd;
      for (uint task_group_i = 0; task_group_i < task_groups; task_group_i++) {
        cmd.instance_id = dtid;
        cmd.task_offset = mesh_data.meshlet_base + task_group_i * K_TASK_TG_SIZE;
        cmd.group_base = task_group_i * K_TASK_TG_SIZE;
        cmd.task_count =
            min(K_TASK_TG_SIZE, mesh_data.meshlet_count - task_group_i * K_TASK_TG_SIZE);
        bindless_rwbuffers[task_cmd_buf_idx].Store<TaskCmd>(
            (task_group_base_i + task_group_i) * sizeof(TaskCmd), cmd);
      }
    }

    // write visibility result for late pass
    if (late) {
      instance_vis_buf.Store(vis_byte_addr, visible ? 1 : 0);
    }
  }
}
