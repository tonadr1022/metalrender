#include <metal_stdlib>
using namespace metal;

#include "shader_constants.h"
#include "dispatch_vertex_shared.h"
#include "math/math.mtl"
#include "mesh_shared.h"
#include "dispatch_shader_shared.h"

struct ICBContainer {
    command_buffer cmd_buf [[id(0)]];
};

struct DispatchVertexShaderArguments {
    device const char* main_vertex_buf [[id(DispatchVertexShaderArgs_MainVertexBuf)]];
    device const uint* index_buf [[id(DispatchVertexShaderArgs_MainIndexBuf)]];
    device const MeshData* mesh_data_buf [[id(DispatchVertexShaderArgs_MeshDataBuf)]];
    device const char* scene_arg_buf [[id(DispatchVertexShaderArgs_SceneArgBuf)]];
    device const InstanceData* obj_infos [[id(DispatchVertexShaderArgs_InstanceDataBuf)]];
};

kernel
void dispatch_vertex_main(uint tp_grid [[thread_position_in_grid]],
                          device ICBContainer *icb_container [[buffer(0)]],
                          device const DispatchVertexShaderArguments& draw_args [[buffer(1)]],
                          constant DispatchMeshParams& params [[buffer(2)]],
                          device const uchar* uniform_buf [[buffer(3)]],
                          device const CullData* cull_data [[buffer(4)]]) {
    // TODO: less branching
    if (tp_grid >= params.tot_meshes) {
        return;
    }
    device const InstanceData& instance_data = draw_args.obj_infos[tp_grid];
    if (instance_data.instance_id == UINT_MAX) {
        return;
    }

    device const MeshData& mesh_data = draw_args.mesh_data_buf[instance_data.mesh_id];

    int passed = 1;
    if (params.frustum_cull) {
        float3 world_center = rotate_quat(instance_data.scale * mesh_data.center, instance_data.rotation)
                            + instance_data.translation;
        float3 center = (cull_data->view * float4(world_center, 1.0)).xyz;
        float radius = mesh_data.radius * instance_data.scale;
        int passed = 1;
        passed = passed && (center.z * cull_data->frustum[1] - abs(center.x) * cull_data->frustum[0]) > -radius;
        passed = passed && (center.z * cull_data->frustum[3] - abs(center.y) * cull_data->frustum[2]) > -radius;
        if (!passed) {
                return;
        }
    }

    if (passed) {
        render_command cmd(icb_container->cmd_buf, tp_grid);

        cmd.set_vertex_buffer(draw_args.main_vertex_buf, 0);
        cmd.set_vertex_buffer(uniform_buf, 1);
        cmd.set_vertex_buffer(draw_args.obj_infos + tp_grid, 2);

        cmd.set_fragment_buffer(draw_args.scene_arg_buf, 0);
        cmd.set_fragment_buffer(uniform_buf, 1);
        cmd.draw_indexed_primitives(primitive_type::triangle,
                                    mesh_data.index_count,
                                    draw_args.index_buf + mesh_data.index_offset,
                                    1, // instance_count
                                    mesh_data.vertex_base,
                                    0); // base_instance
    }
}
