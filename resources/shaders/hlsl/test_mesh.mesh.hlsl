#include "root_sig.hlsl"
#include "shared_test_mesh.h"

[RootSignature(ROOT_SIGNATURE)][NumThreads(128, 1, 1)][outputtopology("triangle")] void main(
    uint gtid : SV_GroupThreadID, out indices uint3 tris[1], out vertices VOut verts[3]) {
  SetMeshOutputCounts(3, 1);
  if (gtid < 3) {
    VOut vout;
    if (gtid == 0) {
      vout.pos = float4(-0.5, -0.5, 0.0, 1.0);
      vout.color = float4(1, 0, 0, 1);
    }
    if (gtid == 1) {
      vout.pos = float4(0.5, -0.5, 0.0, 1.0);
      vout.color = float4(0, 0, 1, 1);
    }
    if (gtid == 2) {
      vout.pos = float4(0.0, 0.5, 0.0, 1.0);
      vout.color = float4(0, 1, 0, 1);
    }
    tris[gtid] = uint3(0, 1, 2);
    verts[gtid] = vout;
  }
}
