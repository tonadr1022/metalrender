#include "root_sig.h"
#include "../shader_constants.h"
#include "../shader_core.h"
#include "shared_test_task.h"
#include "../default_vertex.h"
#include "math.hlsli"
#include "shared_instance_data.h"
#include "shared_mesh_data.h"

uint get_vertex_idx(Meshlet meshlet, uint gtid) {
    uint idx = meshlet.vertex_offset + gtid;
    StructuredBuffer<uint> meshlet_vertex_buf = ResourceDescriptorHeap[meshlet_vertex_buf_idx];
    return meshlet_vertex_buf[idx];
}

VOut get_vertex_attributes(uint vertex_idx) {
    StructuredBuffer<DefaultVertex> vertex_buf = ResourceDescriptorHeap[vertex_buf_idx];
    DefaultVertex vert = vertex_buf[vertex_idx];
    
    StructuredBuffer<InstanceData> instance_data_buf = ResourceDescriptorHeap[instance_data_buf_idx];
    InstanceData instance_data = instance_data_buf[instance_data_idx];

    float3 pos = rotate_quat(instance_data.scale * vert.pos.xyz, instance_data.rotation) + instance_data.translation;
    VOut v;
    v.pos = mul(vp, float4(pos, 1.0));
    v.uv = float2(0.0, 0.0);
    v.color = float4(float((vertex_idx % 3) == 0), 
                     float((vertex_idx % 3) == 1),
                     float((vertex_idx % 3) == 2),
                     1.0);
    return v;
}

uint3 LoadThreeBytes(ByteAddressBuffer buf, uint byte_offset) {
    // word == 4 bytes
    uint word = byte_offset & ~(sizeof(uint) - 1);
    uint word_next = word + sizeof(uint);

    uint low = buf.Load(word);
    uint high = buf.Load(word_next);

    uint64_t combined = ((uint64_t)(high) << (sizeof(uint)*8)) | (uint64_t)low;

    uint alignment = byte_offset & 3; 

    uint i0 = (uint)((combined >> (alignment * 8)) & 0xFF);
    uint i1 = (uint)((combined >> ((alignment + 1)*8)) & 0xFF);
    uint i2 = (uint)((combined >> ((alignment + 2)*8)) & 0xFF);
    return uint3(i0, i1, i2);
}


[RootSignature(ROOT_SIGNATURE)]
[NumThreads(K_MESH_TG_SIZE, 1, 1)]
[outputtopology("triangle")]
void main(uint gtid : SV_GroupThreadID,
            uint gid : SV_GroupID,
            in payload Payload payload,
            out indices uint3 tris[K_MAX_TRIS_PER_MESHLET],
            out vertices VOut verts[K_MAX_VERTS_PER_MESHLET]) {
    StructuredBuffer<MeshData> task_cmds = ResourceDescriptorHeap[task_cmd_buf_idx];
    MeshData task_cmd = task_cmds[task_cmd_idx];

    uint meshlet_idx = payload.meshlet_indices[gid];

    if (meshlet_idx >= task_cmd.meshlet_count) {
        return;
    }

    StructuredBuffer<Meshlet> meshlet_buf = ResourceDescriptorHeap[meshlet_buf_idx];
    Meshlet meshlet = meshlet_buf[meshlet_idx];

    SetMeshOutputCounts(meshlet.vertex_count, meshlet.triangle_count);

    if (gtid < meshlet.vertex_count) {
        uint vertex_idx = get_vertex_idx(meshlet, gtid);
        verts[gtid] = get_vertex_attributes(vertex_idx);
    }

    if (gtid < meshlet.triangle_count) {
        ByteAddressBuffer meshlet_tris = ResourceDescriptorHeap[meshlet_tri_buf_idx];
        tris[gtid] = LoadThreeBytes(meshlet_tris, meshlet.triangle_offset + gtid * 3);
    }
}

