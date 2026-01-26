#include <metal_stdlib>
using namespace metal;

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
    uint stride;
};

struct RootLayout {
    packed_uint2 pad[11]; // 160 bytes
    device void* root_cbvs[3];
    device void* resource_table_ptr;
    device void* sampler_table_ptr;
};

static_assert(sizeof(RootLayout) == 128);


kernel void comp_main(const device uint2* pc [[buffer(0)]],
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
    device packed_uint2* out_ptr = reinterpret_cast<device packed_uint2*>(out_args + gid * sizeof(RootLayout));
    for (uint i = 0; i < 10; i++) {
        out_ptr[i] = pc[i];
    }
    const device IndexedIndirectDrawCmd& cmd = in_cmds[gid];
    device RootLayout* root_layout = reinterpret_cast<device RootLayout*>(out_ptr);
    root_layout->pad[10].x = cmd.first_instance;
    root_layout->pad[10].y = cmd.vertex_offset / args2.stride;

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
