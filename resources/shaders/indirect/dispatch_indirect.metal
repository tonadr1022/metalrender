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
    uint tlab_size;
    uint draw_cnt;
};

// top level argument buffer
struct TLAB {
  uint64_t push_constant_buf;
  uint64_t buffer_descriptor_table;
  uint64_t texture_descriptor_table;
  uint64_t sampler_descriptor_table;
};

struct BasicIndirectPC {
  float4x4 vp;
  uint vert_buf_idx;
  uint instance_data_buf_idx;
  uint mat_buf_idx;
  uint inst_id;
  uint _pad[20];
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
    uint tlab_size = sizeof(TLAB);
    uint draw_cnt = args2.draw_cnt;
    if (gid >= draw_cnt) {
            return;
    }
    device uint8_t* out_ptr = out_args + gid * tlab_size;
    for (uint i = 0; i < tlab_size; i++) {
        out_ptr[i] = pc[i];
    }
    device TLAB* tlab = reinterpret_cast<device TLAB*>(out_ptr);
    device BasicIndirectPC* push = reinterpret_cast<device BasicIndirectPC*>(tlab->push_constant_buf);
    push->inst_id = gid;

    const device IndexedIndirectDrawCmd& cmd = in_cmds[gid];
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
                                cmd.vertex_offset,
                                // TODO: when culling, this needs updating, ie atomic
                                0);
}
