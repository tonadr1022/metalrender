// clang-format off
#define COMPUTE_ROOT_SIG
#include "root_sig.hlsl"
#include "shader_constants.h"
#include "shader_core.h"
#include "default_vertex.h"
#include "math.hlsli"
#include "shared_instance_data.h"
#include "shared_forward_meshlet.h"
#include "shared_task_cmd.h"
#include "shared_mesh_data.h"
#include "shared_globals.h"
// clang-format on

// https://www.ronja-tutorials.com/post/041-hsv-colorspace/
float3 hue2rgb(float hue) {
  hue = frac(hue);                 // only use fractional part of hue, making it loop
  float r = abs(hue * 6 - 3) - 1;  // red
  float g = 2 - abs(hue * 6 - 2);  // green
  float b = 2 - abs(hue * 6 - 4);  // blue
  float3 rgb = float3(r, g, b);    // combine components
  rgb = saturate(rgb);             // clamp between 0 and 1
  return rgb;
}

#ifdef DEBUG_MODE
VOut get_vertex_attributes(in InstanceData instance_data, in float4x4 vp, uint render_mode,
                           uint vertex_idx, uint meshlet_idx, uint instance_data_idx,
                           uint triangle_idx) {
#else
VOut get_vertex_attributes(in InstanceData instance_data, in float4x4 vp, uint render_mode,
                           uint vertex_idx) {
#endif
  DefaultVertex vert =
      bindless_buffers[vertex_buf_idx].Load<DefaultVertex>(vertex_idx * sizeof(DefaultVertex));

  float3 pos = rotate_quat(instance_data.scale * vert.pos.xyz, instance_data.rotation) +
               instance_data.translation;
  VOut v;
  v.pos = mul(vp, float4(pos, 1.0));
  v.uv = vert.uv;
#ifdef DEBUG_MODE
#define COLOR_MULTIPLIER 1.71f
  if (render_mode == DEBUG_RENDER_MODE_TRIANGLE_COLORS) {
    v.color =
        float4(hue2rgb((instance_data_idx + meshlet_idx + triangle_idx) * COLOR_MULTIPLIER), 1.0);
  } else if (render_mode == DEBUG_RENDER_MODE_MESHLET_COLORS) {
    v.color = float4(hue2rgb((instance_data_idx + meshlet_idx) * COLOR_MULTIPLIER), 1.0);
  } else if (render_mode == DEBUG_RENDER_MODE_INSTANCE_COLORS) {
    v.color = float4(hue2rgb(instance_data_idx * COLOR_MULTIPLIER), 1.0);
  } else {
    v.color = float4(1.0, 1.0, 1.0, 1.0);
  }
#endif
  v.material_id = instance_data.mat_id;
  return v;
}

uint3 LoadThreeBytes(ByteAddressBuffer buf, uint byte_offset) {
  // word == 4 bytes
  uint word = byte_offset & ~(sizeof(uint) - 1);
  uint word_next = word + sizeof(uint);

  uint low = buf.Load(word);
  uint high = buf.Load(word_next);

  uint64_t combined = ((uint64_t)(high) << (sizeof(uint) * 8)) | (uint64_t)low;

  uint alignment = byte_offset & 3;

  uint i0 = (uint)((combined >> (alignment * 8)) & 0xFF);
  uint i1 = (uint)((combined >> ((alignment + 1) * 8)) & 0xFF);
  uint i2 = (uint)((combined >> ((alignment + 2) * 8)) & 0xFF);
  return uint3(i0, i1, i2);
}

[RootSignature(ROOT_SIGNATURE)][NumThreads(K_MESH_TG_SIZE, 1, 1)][outputtopology("triangle")] void
main(uint gtid : SV_GroupThreadID, uint gid : SV_GroupID, in payload Payload payload,
     out indices uint3 tris[K_MAX_TRIS_PER_MESHLET],
     out vertices VOut verts[K_MAX_VERTS_PER_MESHLET]) {
  uint task_i = payload.meshlet_indices[gid];
  TaskCmd task_cmd =
      bindless_buffers[task_cmd_buf_idx].Load<TaskCmd>((task_i & 0xffffff) * sizeof(TaskCmd));
  uint meshlet_idx = task_cmd.task_offset + (task_i >> 24);

  InstanceData instance_data = bindless_buffers[instance_data_buf_idx].Load<InstanceData>(
      task_cmd.instance_id * sizeof(InstanceData));

  MeshData mesh_data =
      bindless_buffers[mesh_data_buf_idx].Load<MeshData>(instance_data.mesh_id * sizeof(MeshData));
  Meshlet meshlet = bindless_buffers[meshlet_buf_idx].Load<Meshlet>(meshlet_idx * sizeof(Meshlet));

  SetMeshOutputCounts(meshlet.vertex_count, meshlet.triangle_count);

  GlobalData globals = load_globals();
  for (uint i = gtid; i < meshlet.vertex_count;) {
    uint vertex_idx =
        bindless_buffers_uint[meshlet_vertex_buf_idx]
                             [meshlet.vertex_offset + i + mesh_data.meshlet_vertices_offset];
#ifdef DEBUG_MODE
    verts[i] = get_vertex_attributes(instance_data, globals.vp, globals.render_mode,
                                     vertex_idx + mesh_data.vertex_base, meshlet_idx,
                                     task_cmd.instance_id, i);
#else
    verts[i] = get_vertex_attributes(instance_data, globals.vp, globals.render_mode,
                                     vertex_idx + mesh_data.vertex_base);
#endif
    i += K_MESH_TG_SIZE;
  }

  for (uint i = gtid; i < meshlet.triangle_count;) {
    ByteAddressBuffer meshlet_tris = bindless_buffers[meshlet_tri_buf_idx];
    // The byte value must be a multiple of four so that it is DWORD aligned. If any other value is
    // provided, behavior is undefined.
    tris[i] = LoadThreeBytes(meshlet_tris,
                             mesh_data.meshlet_triangles_offset + meshlet.triangle_offset + i * 3);
    i += K_MESH_TG_SIZE;
  }
}
