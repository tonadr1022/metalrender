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
  if (dtid >= max_draws) return;

  InstanceData instance_data =
      bindless_buffers[instance_data_buf_idx].Load<InstanceData>(dtid * sizeof(InstanceData));
  if (instance_data.mesh_id == 0xFFFFFFFF) return;

  MeshData mesh_data =
      bindless_buffers[mesh_data_buf_idx].Load<MeshData>(instance_data.mesh_id * sizeof(MeshData));

  M4Material material = bindless_buffers[materials_buf_idx].Load<M4Material>(instance_data.mat_id *
                                                                             sizeof(M4Material));
  bool alpha_test_enabled = (material.flags & M4MAT_FLAG_ALPHATEST) != 0;

  uint task_groups = (mesh_data.meshlet_count + K_TASK_TG_SIZE - 1) / K_TASK_TG_SIZE;

  for (uint v = 0; v < view_cull_setup_count; ++v) {
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

    bool object_occlusion_cull_enabled =
        (view_setup.flags & OBJECT_OCCLUSION_CULL_ENABLED_BIT) != 0;
    bool meshlet_occlusion_cull_enabled =
        (view_setup.flags & MESHLET_OCCLUSION_CULL_ENABLED_BIT) != 0;
    uint vis_byte_addr = dtid * sizeof(uint32_t);
    // When object occlusion is disabled, the CPU won't allocate/bind `instance_vis_buf`.
    // Default to visible to ensure early/late scheduling does not get accidentally gated.
    bool visible_last_frame = true;
    if (object_occlusion_cull_enabled) {
      RWByteAddressBuffer instance_vis_buf = bindless_rwbuffers[view_setup.instance_vis_buf_idx];
      visible_last_frame = instance_vis_buf.Load(vis_byte_addr) != 0;
    }
    bool late = view_setup.pass != 0;

    // early pass: only process draws that were visible last frame
    if (!late && !visible_last_frame) continue;

    bool visible = true;

    if (culling_enabled != 0) {
      float3 world_center =
          rotate_quat(instance_data.scale * mesh_data.center, instance_data.rotation) +
          instance_data.translation;

      float radius = mesh_data.radius * instance_data.scale;
      float4 center = mul(view_data.view, float4(world_center, 1.0));  // view space

      visible = visible && (-center.z + radius > cull_data.z_near) &&
                (-center.z - radius < cull_data.z_far);

      visible = visible &&
                (center.z * cull_data.frustum[3] - abs(center.y) * cull_data.frustum[2] > -radius);

      visible = visible &&
                (center.z * cull_data.frustum[1] - abs(center.x) * cull_data.frustum[0] > -radius);

      if (late && visible && object_occlusion_cull_enabled) {
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

          float d0 = depth_pyramid_tex
                         .SampleLevel(samp, center_uv + float2(-halfTexel.x, -halfTexel.y), lod_i)
                         .x;
          float d1 = depth_pyramid_tex
                         .SampleLevel(samp, center_uv + float2(-halfTexel.x, halfTexel.y), lod_i)
                         .x;
          float d2 = depth_pyramid_tex
                         .SampleLevel(samp, center_uv + float2(halfTexel.x, -halfTexel.y), lod_i)
                         .x;
          float d3 = depth_pyramid_tex
                         .SampleLevel(samp, center_uv + float2(halfTexel.x, halfTexel.y), lod_i)
                         .x;

          float depth = min(min(d0, d1), min(d2, d3));

          // Same sphere depth as forward_meshlet.task.hlsl (occlusion test)
          float view_z = center.z + radius;
          float zn = cull_data.z_near;
          float depth_sphere = zn / -view_z;
          visible = visible && (depth_sphere >= depth);
        }
      }
    }

    bool should_emit = visible;

    if (late) {
      // Late pass should only emit newly visible objects, unless meshlet occlusion
      // is enabled (then we still need a late pass to recover newly visible meshlets).
      should_emit = meshlet_occlusion_cull_enabled ? visible : (visible && !visible_last_frame);
    }

    if (should_emit) {
      if (visible) {
        RWByteAddressBuffer draw_cmd_count_buf =
            bindless_rwbuffers[view_setup.draw_cmd_count_buf_idx];
        draw_cmd_count_buf.InterlockedAdd(sizeof(uint32_t) * view_setup.pass, 1);
      }

      RWByteAddressBuffer task_cmd_cnt_buf = bindless_rwbuffers[view_setup.draw_cnt_buf_idx];
      uint task_group_base_i;
      task_cmd_cnt_buf.InterlockedAdd(uint(alpha_test_enabled) * sizeof(DispatchMeshCmd),
                                      task_groups, task_group_base_i);

      TaskCmd cmd;
      cmd.instance_id = dtid;
      cmd.late_draw_visibility = uint(visible_last_frame);
      for (uint i = 0; i < task_groups; ++i) {
        cmd.task_offset = mesh_data.meshlet_base + i * K_TASK_TG_SIZE;
        cmd.group_base = i * K_TASK_TG_SIZE;
        cmd.task_count = min(K_TASK_TG_SIZE, mesh_data.meshlet_count - i * K_TASK_TG_SIZE);

        bindless_rwbuffers[task_cmd_buf_idx].Store<TaskCmd>(
            (task_group_base_i + i) * sizeof(TaskCmd), cmd);
      }
    }

    if (late && object_occlusion_cull_enabled) {
      RWByteAddressBuffer instance_vis_buf = bindless_rwbuffers[view_setup.instance_vis_buf_idx];
      instance_vis_buf.Store(vis_byte_addr, visible ? 1 : 0);
    }
  }
}
