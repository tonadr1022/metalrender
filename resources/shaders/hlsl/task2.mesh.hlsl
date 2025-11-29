// clang-format off
#include "root_sig.h"
#include "../shader_constants.h"
#include "../shader_core.h"
#include "../default_vertex.h"
#include "math.hlsli"
#include "shared_instance_data.h"
#include "shared_task2.h"
#include "shared_task_cmd.h"
#include "shared_mesh_data.h"
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

VOut get_vertex_attributes(in InstanceData instance_data, uint vertex_idx, uint meshlet_idx,
                           uint instance_data_idx) {
  StructuredBuffer<DefaultVertex> vertex_buf = ResourceDescriptorHeap[vertex_buf_idx];
  DefaultVertex vert = vertex_buf[vertex_idx];

  float3 pos = rotate_quat(instance_data.scale * vert.pos.xyz, instance_data.rotation) +
               instance_data.translation;
  VOut v;
  v.pos = mul(vp, float4(pos, 1.0));
  v.uv = vert.uv;
  // v.color = float4(float((vertex_idx % 3) == 0), float((vertex_idx % 3) == 1),
  //                   float((vertex_idx % 3) == 2), 1.0);
  v.color = float4(hue2rgb((instance_data_idx) * 1.71f), 1.0);
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
  StructuredBuffer<MeshData> task_cmds = ResourceDescriptorHeap[mesh_data_buf_idx];
  StructuredBuffer<TaskCmd> tts = ResourceDescriptorHeap[tt_cmd_buf_idx];

  uint task_i = payload.meshlet_indices[gid];
  TaskCmd tt = tts[task_i & 0xffffff];
  uint meshlet_idx = tt.task_offset + (task_i >> 24);

  StructuredBuffer<InstanceData> instance_data_buf = ResourceDescriptorHeap[instance_data_buf_idx];
  InstanceData instance_data = instance_data_buf[tt.instance_id];

  MeshData task_cmd = task_cmds[instance_data.mesh_id];

  StructuredBuffer<Meshlet> meshlet_buf = ResourceDescriptorHeap[meshlet_buf_idx];
  Meshlet meshlet = meshlet_buf[meshlet_idx];

  SetMeshOutputCounts(meshlet.vertex_count, meshlet.triangle_count);

  StructuredBuffer<uint> meshlet_vertex_buf = ResourceDescriptorHeap[meshlet_vertex_buf_idx];
  for (uint i = gtid; i < meshlet.vertex_count;) {
    uint vertex_idx =
        meshlet_vertex_buf[meshlet.vertex_offset + i + task_cmd.meshlet_vertices_offset];
    verts[i] = get_vertex_attributes(instance_data, vertex_idx + task_cmd.vertex_base, meshlet_idx,
                                     tt.instance_id);
    i += K_MESH_TG_SIZE;
  }

  if (gtid < meshlet.vertex_count) {
  }

  for (uint i = gtid; i < meshlet.triangle_count;) {
    ByteAddressBuffer meshlet_tris = ResourceDescriptorHeap[meshlet_tri_buf_idx];
    // The byte value must be a multiple of four so that it is DWORD aligned. If any other value is
    // provided, behavior is undefined.
    tris[i] = LoadThreeBytes(meshlet_tris,
                             task_cmd.meshlet_triangles_offset + meshlet.triangle_offset + i * 3);

    i += K_MESH_TG_SIZE;
  }
  if (gtid < meshlet.triangle_count) {
  }
}
