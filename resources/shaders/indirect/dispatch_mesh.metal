#include <metal_stdlib>
using namespace metal;


struct Args {
    command_buffer cmd_buf;
};

struct Args2 {
    uint3 threads_per_object_thread_group;
    uint3 threads_per_mesh_thread_group;
    uint draw_cnt;
};

struct MeshArg {
    uint3 thread_groups_per_task;
};

struct TLAB_Layout {
    packed_uint4 pad[10]; // 160 bytes
};

kernel void comp_main(device Args& args [[buffer(0)]],
                      constant Args2& args2 [[buffer(1)]],
                      const device TLAB_Layout* tlab [[buffer(2)]],
                      const device MeshArg* in_mesh_args [[buffer(3)]],
                      const device uint8_t* resource_desc_heap [[buffer(4)]],
                      const device uint8_t* sampler_desc_heap [[buffer(5)]],
                      uint tid [[thread_position_in_grid]]) {
    if (tid >= args2.draw_cnt) {
        return;
    }

    render_command cmd(args.cmd_buf, tid);

    cmd.set_object_buffer(resource_desc_heap, 0);
    cmd.set_mesh_buffer(resource_desc_heap, 0);
    cmd.set_fragment_buffer(resource_desc_heap, 0);
    cmd.set_object_buffer(sampler_desc_heap, 1);
    cmd.set_mesh_buffer(sampler_desc_heap, 1);
    cmd.set_fragment_buffer(sampler_desc_heap, 1);
    cmd.set_object_buffer(tlab, 2);
    cmd.set_mesh_buffer(tlab, 2);
    cmd.set_fragment_buffer(tlab, 2);
    cmd.draw_mesh_threadgroups(in_mesh_args[tid].thread_groups_per_task,
                               args2.threads_per_object_thread_group,
                               args2.threads_per_mesh_thread_group);
}

