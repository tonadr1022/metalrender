#include "root_sig.h"
#include "shared_test_task.h"

[RootSignature(ROOT_SIGNATURE)]
float4 main(VOut input) : SV_Target0 {
    return input.color;
}
