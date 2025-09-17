#include <metal_stdlib>
using namespace metal;

#include "shader_constants.h"
#include "default_vertex.h"
#include "shader_global_uniforms.h"

// should this change?
struct ObjectPayload {
    uint meshlet_base; // unit == element;
    uint meshlet_count;
};

struct MeshletDesc {
    uint vertex_offset;
    uint triangle_offset;
    uint vertex_count;
    uint triangle_count;
};

// https://www.ronja-tutorials.com/post/041-hsv-colorspace/
static float3 hue2rgb(float hue) {
    hue = fract(hue); //only use fractional part of hue, making it loop
    float r = abs(hue * 6 - 3) - 1; //red
    float g = 2 - abs(hue * 6 - 2); //green
    float b = 2 - abs(hue * 6 - 4); //blue
    float3 rgb = float3(r,g,b); //combine components
    rgb = saturate(rgb); //clamp between 0 and 1
    return rgb;
}

struct ObjectParams {
    uint meshlet_base;
    uint meshlet_count;
};

[[object]]
void basic1_object_main(object_data ObjectPayload& payload [[payload]],
                 constant ObjectParams& params [[buffer(0)]],
                 uint thread_idx [[thread_position_in_threadgroup]],
                 mesh_grid_properties grid) {

    if (thread_idx == 0) {
        payload.meshlet_base = params.meshlet_base;
        payload.meshlet_count = params.meshlet_count;
        grid.set_threadgroups_per_grid(uint3(params.meshlet_count, 1, 1));
    }

}

struct MeshletPrimitive {
    float4 color [[flat]];
};

using Meshlet = metal::mesh<MeshletVertex,
                            MeshletPrimitive,
                            k_max_vertices_per_meshlet,
                            k_max_triangles_per_meshlet,
                            topology::triangle
                            >;
[[mesh]]
void basic1_mesh_main(const object_data ObjectPayload& object [[payload]],
                        device const DefaultVertex* vertices [[buffer(0)]],
                        constant Uniforms& uniforms [[buffer(1)]],
                        constant MeshletDesc* meshlets [[buffer(2)]],
                        device const uint* meshlet_vertices [[buffer(3)]],
                        device const uchar* meshlet_triangles [[buffer(4)]],
                        uint payload_idx [[threadgroup_position_in_grid]],
                        uint thread_idx [[thread_position_in_threadgroup]],
                        Meshlet out_mesh) {
    uint meshlet_idx = payload_idx + object.meshlet_base;
    constant MeshletDesc& meshlet = meshlets[meshlet_idx];
    if (thread_idx < meshlet.vertex_count) {
        device const DefaultVertex& vert = vertices[meshlet_vertices[meshlet.vertex_offset + thread_idx]];
        MeshletVertex out_vert;
        out_vert.pos = uniforms.vp * float4(vert.pos.xyz, 1.0);
        out_vert.uv = vert.uv;
        out_vert.normal = vert.normal;
        out_mesh.set_vertex(thread_idx, out_vert);
    }

    if (thread_idx < meshlet.triangle_count) {
        uint i = thread_idx * 3;
        out_mesh.set_index(i + 0, meshlet_triangles[meshlet.triangle_offset + i + 0]);
        out_mesh.set_index(i + 1, meshlet_triangles[meshlet.triangle_offset + i + 1]);
        out_mesh.set_index(i + 2, meshlet_triangles[meshlet.triangle_offset + i + 2]);
        MeshletPrimitive prim = {
            .color = float4(hue2rgb(meshlet_idx * 1.71f), 1)
        };
        out_mesh.set_primitive(thread_idx, prim);
    }

    if (thread_idx == 0) {
        out_mesh.set_primitive_count(meshlet.triangle_count);
    }

}

struct FragmentIn {
    MeshletVertex vert;
    MeshletPrimitive prim;
};

[[fragment]]
float4 basic1_fragment_main(FragmentIn in [[stage_in]]) {
    return in.prim.color;
}
