#include <metal_stdlib>
using namespace metal;

#include "../default_vertex.h"

struct IndexedIndirectDrawCmd {
  uint32_t index_count;
  uint32_t instance_count;
  uint32_t first_index;
  int32_t vertex_offset;
  uint32_t first_instance;
};

struct Args {
    command_buffer cmd_buf;
};

struct Args2 {
    uint draw_cnt;
};

#define PC_SIZE 160
struct TLAB_Layout {
    packed_uint4 pad[10]; // 160 bytes
    uint draw_id;
    uint vertex_id_base;
};


kernel void comp_main(const device uint8_t* pc [[buffer(0)]],
                      constant Args2& args2 [[buffer(1)]],
                      device Args& args [[buffer(2)]],
                      device uint8_t* out_args [[buffer(3)]],
                      const device IndexedIndirectDrawCmd* in_cmds [[buffer(4)]],
                      const device uint8_t* index_buf [[buffer(5)]],
                      const device uint8_t* resource_desc_heap [[buffer(6)]],
                      const device uint8_t* sampler_desc_heap [[buffer(7)]],
                      uint gid [[thread_position_in_grid]]) {
    uint draw_cnt = args2.draw_cnt;
    if (gid >= draw_cnt) {
            return;
    }
    device uint8_t* out_ptr = out_args + gid * sizeof(TLAB_Layout);
    for (uint i = 0; i < PC_SIZE; i++) {
        out_ptr[i] = pc[i];
    }
    const device IndexedIndirectDrawCmd& cmd = in_cmds[gid];
    device TLAB_Layout* tlab_lay = reinterpret_cast<device TLAB_Layout*>(out_ptr);
    tlab_lay->draw_id = cmd.first_instance;
    tlab_lay->vertex_id_base = cmd.vertex_offset / sizeof(DefaultVertex);

    render_command ren_cmd(args.cmd_buf, gid);
    ren_cmd.reset();
    ren_cmd.set_vertex_buffer(resource_desc_heap, 0);
    ren_cmd.set_fragment_buffer(resource_desc_heap, 0);
    ren_cmd.set_vertex_buffer(sampler_desc_heap, 1);
    ren_cmd.set_fragment_buffer(sampler_desc_heap, 1);
    ren_cmd.set_vertex_buffer(out_ptr, 2);
    ren_cmd.set_fragment_buffer(out_ptr, 2);

    ren_cmd.draw_indexed_primitives(primitive_type::triangle,
                                cmd.index_count,
                                reinterpret_cast<const device uint*>(index_buf + cmd.first_index * sizeof(uint)),
                                1,
                                0, // does nothing
                                0); // does nothing
}
