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
#include "shared_meshlet_draw_stats.hlsli"
#include "default_vertex.h"
// clang-format on

#define UINT_MAX 0xFFFFFFFFu

groupshared Payload s_Payload;
groupshared uint s_visible_meshlet_cnt;
groupshared uint s_tris[K_TASK_TG_SIZE];

CONSTANT_BUFFER(ViewData, view_data, VIEW_DATA_SLOT);
CONSTANT_BUFFER(GlobalData, global_data, GLOBALS_SLOT);
CONSTANT_BUFFER(CullData, cull_data, 4);
StructuredBuffer<Meshlet> meshlet_buf : register(t6);
StructuredBuffer<InstanceData> instance_data_buf : register(t10);
StructuredBuffer<TaskCmd> task_cmd_buf : register(t4);
RWStructuredBuffer<uint> meshlet_vis_buf : register(u1);
RWStructuredBuffer<uint> meshlet_draw_stats : register(u2);

#ifdef LATE
Texture2D depth_pyramid_tex : register(t3);
#endif

bool sphere_visible_perspective_frustum(float3 center, float radius, CullData cd) {
  // Ref:
  // https://github.com/zeux/niagara/blob/master/src/shaders/clustercull.comp.glsl#L101C1-L102C102
  return ((-center.z + radius) > cd.z_near && (-center.z - radius) < cd.z_far) &&
         (center.z * cd.frustum[3] - abs(center.y) * cd.frustum[2]) > -radius &&
         (center.z * cd.frustum[1] - abs(center.x) * cd.frustum[0]) > -radius;
}

bool sphere_visible_ortho_frustum(float3 center, float radius, CullData cd) {
  return (center.x + radius) > cd.ortho_bounds.x && (center.x - radius) < cd.ortho_bounds.y &&
         (center.y + radius) > cd.ortho_bounds.z && (center.y - radius) < cd.ortho_bounds.w &&
         (center.z + radius) > cd.z_near && (center.z - radius) < cd.z_far;
}

bool sphere_visible_frustum(float3 center, float radius, CullData cd) {
  if (cd.projection_type == CULL_PROJECTION_ORTHOGRAPHIC) {
    return sphere_visible_ortho_frustum(center, radius, cd);
  }

  return sphere_visible_perspective_frustum(center, radius, cd);
}

[NumThreads(K_TASK_TG_SIZE, 1, 1)] void main(uint gtid
                                             : SV_GroupThreadID, uint dtid
                                             : SV_DispatchThreadID, uint gid
                                             : SV_GroupID) {
  // pass 1: draw meshlets visible last frame and write to depth buffer
  // pass 2: draw  meshlets not visible last frame that pass the cull test against depth pyramid.
  uint task_group_id = gid;

  bool visible = false;
  bool draw = false;
  uint meshlet_vis_i = UINT_MAX;

  SamplerState samp = bindless_samplers[NEAREST_CLAMP_EDGE_SAMPLER_IDX];

  StructuredBuffer<uint3> draw_cnt_buf = bindless_buffers_uint3[pc.out_draw_count_buf_idx];
  bool valid_task_group = task_group_id < draw_cnt_buf[pc.alpha_test_enabled].x;
  uint lane_tri_contrib = 0u;

  if (valid_task_group) {
    TaskCmd task_cmd = task_cmd_buf[task_group_id];
    if (gtid < task_cmd.task_count) {
      visible = true;

      uint meshlet_index = task_cmd.task_offset + gtid;
      Meshlet meshlet = meshlet_buf[meshlet_index];
      InstanceData instance_data = instance_data_buf[task_cmd.instance_id];

      meshlet_vis_i = instance_data.meshlet_vis_base + gtid + task_cmd.group_base;
      bool instance_visible_last = (task_cmd.late_draw_visibility != 0);
      bool visible_last_frame = true;
      bool meshlet_occlusion_cull_enabled = (pc.flags & MESHLET_OCCLUSION_CULL_ENABLED_BIT) != 0;
      if (meshlet_occlusion_cull_enabled) {
        visible_last_frame = instance_visible_last ? (meshlet_vis_buf[meshlet_vis_i] != 0) : false;
      }
      bool skip_draw = false;

      // Per-meshlet early/late only when meshlet occlusion is enabled; otherwise instance
      // early/late is already split in draw_cull task buffers.
      if (meshlet_occlusion_cull_enabled) {
#ifndef LATE
        if (!visible_last_frame) {
          visible = false;
        }
#endif
#ifdef LATE
        if (visible_last_frame) {
          skip_draw = true;
        }
#endif
      }
      float3 world_center =
          rotate_quat(instance_data.scale * meshlet.center_radius.xyz, instance_data.rotation) +
          instance_data.translation;
      float radius = meshlet.center_radius.w * instance_data.scale;
      float3 center = mul(view_data.view, float4(world_center, 1.0)).xyz;
      if ((pc.flags & MESHLET_FRUSTUM_CULL_ENABLED_BIT) != 0) {
        visible = visible && sphere_visible_frustum(center, radius, cull_data);
      }

      // normal cone culling
      if ((pc.flags & MESHLET_CONE_CULL_ENABLED_BIT) != 0) {
        // 8-bit SNORM quantized
        float3 cone_axis = float3(int(meshlet.cone_axis_cutoff << 24) >> 24,
                                  int(meshlet.cone_axis_cutoff << 16) >> 24,
                                  int(meshlet.cone_axis_cutoff << 8) >> 24) /
                           127.0;
        float cone_cutoff = float(int(meshlet.cone_axis_cutoff) >> 24) / 127.0;
        cone_axis = rotate_quat(cone_axis, instance_data.rotation);
        /*
        cone_axis =
            mul(float3x3(view_data.view[0].xyz, view_data.view[1].xyz, view_data.view[2].xyz),
                cone_axis);
        */
        cone_axis = mul((float3x3)view_data.view, cone_axis);
        visible = visible && !cone_cull(center, radius, cone_axis, cone_cutoff, float3(0, 0, 0));
      }

// late_draw_visibility==0: object HZB in draw_cull already passed; meshlet HZB is stricter
// and false-culls here when both modes are on. ==1: still need meshlet HZB after early pass.
#ifdef LATE
      if ((pc.flags & MESHLET_OCCLUSION_CULL_ENABLED_BIT) != 0 && visible &&
          task_cmd.late_draw_visibility != 0) {
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
          // Vulkan path currently renders the depth attachment upside down relative to the
          // projected UVs produced by `project_sphere()`, so flip Y before sampling the HZB.
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
#endif

      if (cull_data.paused == 0 && (pc.flags & MESHLET_OCCLUSION_CULL_ENABLED_BIT) != 0) {
        // Only update visibility when NOT paused
        // visible stays as calculated above
#ifdef LATE
        if (meshlet_vis_i != UINT_MAX) {
          meshlet_vis_buf[meshlet_vis_i] = visible;
        }
#endif
      } else if (cull_data.paused != 0) {
        // When paused, use the last frame's visibility state
        visible = visible_last_frame;
      }

      draw = visible && !skip_draw;
      lane_tri_contrib = draw ? meshlet.vertex_count : 0u;
    }
  }

  s_tris[gtid] = lane_tri_contrib;

  if (gtid == 0) {
    s_visible_meshlet_cnt = 0;
  }

  // wait for s_count initialization to 0
  GroupMemoryBarrierWithGroupSync();

  if (draw) {
    uint thread_i;
    InterlockedAdd(s_visible_meshlet_cnt, 1, thread_i);
    s_Payload.meshlet_indices[thread_i] = (task_group_id & 0xFFFFFFu) | (gtid << 24);
  }

  // wait for s_Payload writes to finish
  GroupMemoryBarrierWithGroupSync();

  uint visible_meshlet_cnt = s_visible_meshlet_cnt;

  // sum triangles for meshlet draw stats
  {
    for (uint offset = K_TASK_TG_SIZE / 2u; offset > 0u; offset >>= 1u) {
      if (gtid < offset) {
        s_tris[gtid] += s_tris[gtid + offset];
      }
      GroupMemoryBarrierWithGroupSync();
    }

    if (gtid == 0 && valid_task_group && global_data.meshlet_stats_enabled != 0) {
      uint tri_sum = s_tris[0];
      if (visible_meshlet_cnt != 0u || tri_sum != 0u) {
        MeshletDrawStats_AtomicAdd(meshlet_draw_stats, MESHLET_DRAW_STATS_CATEGORY_MESHLETS,
#ifdef LATE
                                   1
#else
                                   0
#endif
                                   ,
                                   visible_meshlet_cnt);
        MeshletDrawStats_AtomicAdd(meshlet_draw_stats, MESHLET_DRAW_STATS_CATEGORY_TRIANGLES,
#ifdef LATE
                                   1
#else
                                   0
#endif
                                   ,
                                   tri_sum);
      }
    }

    GroupMemoryBarrierWithGroupSync();
  }

  DispatchMesh(visible_meshlet_cnt, 1, 1, s_Payload);
}
