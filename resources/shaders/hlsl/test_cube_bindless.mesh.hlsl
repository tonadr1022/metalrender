#include "root_sig.hlsl"
#include "shared_test_cube_bindless.h"

// 12 triangles, 24 vertices (UV seams); CCW from outside, +Y up.
static const uint3 kCubeTris[12] = {
    uint3(0, 1, 2),    uint3(0, 2, 3),     // +Z
    uint3(4, 5, 6),    uint3(4, 6, 7),     // -Z
    uint3(8, 9, 10),   uint3(8, 10, 11),   // +X
    uint3(12, 13, 14), uint3(12, 14, 15),  // -X
    uint3(16, 17, 18), uint3(16, 18, 19),  // +Y
    uint3(20, 21, 22), uint3(20, 22, 23)   // -Y
};

[NumThreads(128, 1, 1)][outputtopology("triangle")] void main(uint gtid
                                                              : SV_GroupThreadID,
                                                                out indices uint3 tris[12],
                                                                out vertices VOut verts[24]) {
  SetMeshOutputCounts(24, 12);
  if (gtid < 24) {
    CubeVertex v = bindless_buffers[pc.vert_buf_idx].Load<CubeVertex>(gtid * sizeof(CubeVertex));
    VOut o;
    o.pos = mul(pc.mvp, v.pos);
    o.uv = v.uv;
    verts[gtid] = o;
  }
  if (gtid < 12) {
    tris[gtid] = kCubeTris[gtid];
  }
}
