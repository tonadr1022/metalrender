#include "root_sig.hlsl"
#include "shared_test_mesh_buf.h"

StructuredBuffer<MeshHelloVertex> g_vertices : register(t0);

[NumThreads(128, 1, 1)][outputtopology("triangle")] void main(uint gtid
                                                              : SV_GroupThreadID,
                                                                out indices uint3 tris[1],
                                                                out vertices VOut verts[3]) {
  SetMeshOutputCounts(3, 1);
  if (gtid < 3) {
    MeshHelloVertex v = g_vertices[gtid];
    VOut vout;
    vout.pos = v.pos;
    vout.uv = float2(0.0, 0.0);
    vout.color = v.color;
    verts[gtid] = vout;
  }
  if (gtid == 0) {
    tris[0] = uint3(0, 1, 2);
  }
}
