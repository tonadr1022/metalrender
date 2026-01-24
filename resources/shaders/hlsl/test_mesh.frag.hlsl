#include "root_sig.hlsl"
#include "shared_test_mesh.h"

[RootSignature(ROOT_SIGNATURE)] float4 main(VOut input) : SV_Target0 { return input.color; }
