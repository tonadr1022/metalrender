#include "../root_sig.hlsl"

struct VOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

[RootSignature(ROOT_SIGNATURE)] float4 main(VOut input) : SV_Target {
  return float4(input.uv, 1, 1);
}
