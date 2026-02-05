// clang-format off
#include "root_sig.hlsl"
#include "shader_constants.h"
#include "shader_core.h"
#include "shared_forward_meshlet.h"
#include "shared_mesh_data.h"
#include "math.hlsli"
#include "shared_task_cmd.h"
#include "shared_instance_data.h"
#include "shared_globals.h"
#include "shared_cull_data.h"
#include "default_vertex.h"
// clang-format on

#define UINT_MAX 0xFFFFFFFFu

groupshared Payload s_Payload;
groupshared uint s_count;

CONSTANT_BUFFER(ViewData, view_data, VIEW_DATA_SLOT);
CONSTANT_BUFFER(GlobalData, global_data, GLOBALS_SLOT);
CONSTANT_BUFFER(CullData, cull_data, 4);
StructuredBuffer<Meshlet> meshlet_buf : register(t6);
StructuredBuffer<InstanceData> instance_data_buf : register(t10);
StructuredBuffer<TaskCmd> task_cmd_buf : register(t4);
RWStructuredBuffer<uint3> draw_cnt_buf : register(u0);
RWStructuredBuffer<uint> meshlet_vis_buf : register(u1);
RWStructuredBuffer<uint> out_counts_buf : register(u2);
Texture2D depth_pyramid_tex : register(t3);

[RootSignature(ROOT_SIGNATURE)][NumThreads(K_TASK_TG_SIZE, 1, 1)] void main(
    uint gtid : SV_GroupThreadID, uint dtid : SV_DispatchThreadID, uint gid : SV_GroupID) {
  // pass 1: draw meshlets visible last frame and write to depth buffer
  // pass 2: draw  meshlets not visible last frame that pass the cull test against depth pyramid.
  uint task_group_id = gid;

  bool visible = false;
  bool draw = false;
  uint meshlet_vis_i = UINT_MAX;

  SamplerState samp = bindless_samplers[NEAREST_CLAMP_EDGE_SAMPLER_IDX];

  if (task_group_id < draw_cnt_buf[0].x) {
    TaskCmd task_cmd = task_cmd_buf[task_group_id];
    if (gtid < task_cmd.task_count) {
      visible = true;

      uint meshlet_index = task_cmd.task_offset + gtid;
      Meshlet meshlet = meshlet_buf[meshlet_index];
      InstanceData instance_data = instance_data_buf[task_cmd.instance_id];

      meshlet_vis_i = instance_data.meshlet_vis_base + gtid + task_cmd.group_base;
      bool visible_last_frame = true;
      if ((flags & MESHLET_OCCLUSION_CULL_ENABLED_BIT) != 0) {
        visible_last_frame = meshlet_vis_buf[meshlet_vis_i] != 0;
      }
      bool skip_draw = false;

      if (pass == 0 && !visible_last_frame) {
        visible = false;
      }
      if (pass != 0 && visible_last_frame) {
        skip_draw = true;
      }
      float3 world_center =
          rotate_quat(instance_data.scale * meshlet.center_radius.xyz, instance_data.rotation) +
          instance_data.translation;
      float radius = meshlet.center_radius.w * instance_data.scale;
      float3 center = mul(view_data.view, float4(world_center, 1.0)).xyz;
      // Ref:
      // https://github.com/zeux/niagara/blob/master/src/shaders/clustercull.comp.glsl#L101C1-L102C102
      // frustum cull, plane symmetry
      if ((flags & MESHLET_FRUSTUM_CULL_ENABLED_BIT) != 0) {
        visible = visible && ((-center.z + radius) > cull_data.z_near &&
                              (-center.z - radius) < cull_data.z_far);
        visible = visible && (center.z * cull_data.frustum[3] -
                              abs(center.y) * cull_data.frustum[2]) > -radius;
        visible = visible && (center.z * cull_data.frustum[1] -
                              abs(center.x) * cull_data.frustum[0]) > -radius;
      }

      // normal cone culling
      if ((flags & MESHLET_CONE_CULL_ENABLED_BIT) != 0) {
        // 8-bit SNORM quantized
        float3 cone_axis = float3(int(meshlet.cone_axis_cutoff << 24) >> 24,
                                  int(meshlet.cone_axis_cutoff << 16) >> 24,
                                  int(meshlet.cone_axis_cutoff << 8) >> 24) /
                           127.0;
        float cone_cutoff = float(int(meshlet.cone_axis_cutoff) >> 24) / 127.0;
        cone_axis = rotate_quat(cone_axis, instance_data.rotation);
        cone_axis =
            mul(float3x3(view_data.view[0].xyz, view_data.view[1].xyz, view_data.view[2].xyz),
                cone_axis);
        visible = visible && !cone_cull(center, radius, cone_axis, cone_cutoff, float3(0, 0, 0));
      }

      if (pass != 0 && (flags & MESHLET_OCCLUSION_CULL_ENABLED_BIT) != 0 && visible) {
        // occlusion cull test
        ProjectSphereResult proj_res =
            project_sphere(center, radius, cull_data.z_near, cull_data.p00, cull_data.p11);
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

      // visible = visible || (cull_data.paused != 0 && visible_last_frame);
      if (cull_data.paused == 0 && (flags & MESHLET_OCCLUSION_CULL_ENABLED_BIT) != 0) {
        // Only update visibility when NOT paused
        // visible stays as calculated above
        if (pass != 0 && meshlet_vis_i != UINT_MAX) {
          meshlet_vis_buf[meshlet_vis_i] = visible;
        }
      } else {
        // When paused, use the last frame's visibility state
        visible = visible_last_frame;
      }

      draw = visible && !skip_draw;
    }
    if (global_data.frame_num % 10 == 0) {
      uint meshlet_idx = task_cmd.task_offset + gtid;
      Meshlet meshlet = meshlet_buf[meshlet_idx];
      uint tri_count = meshlet.vertex_count;
      uint cnt;
      InterlockedAdd(out_counts_buf[pass + 2], tri_count * (draw ? 1 : 0), cnt);
    }
  }

  uint cnt;
  InterlockedAdd(out_counts_buf[pass], draw ? 1 : 0, cnt);

  if (gtid == 0) {
    s_count = 0;
  }

  // wait for s_count initialization to 0
  GroupMemoryBarrierWithGroupSync();

  if (draw) {
    uint thread_i;
    InterlockedAdd(s_count, 1, thread_i);
    s_Payload.meshlet_indices[thread_i] = (task_group_id & 0xFFFFFFu) | (gtid << 24);
  }

  // wait for s_Payload writes to finish
  GroupMemoryBarrierWithGroupSync();

  uint visible_count = s_count;

  DispatchMesh(visible_count, 1, 1, s_Payload);
}
