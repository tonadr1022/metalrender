// clang-format off
#define COMPUTE_ROOT_SIG
#include "root_sig.hlsl"
#include "shared_test_clear_buf.h"
// clang-format on

struct DispatchIndirectCmd {
  uint tg_x;
  uint tg_y;
  uint tg_z;
};

[RootSignature(ROOT_SIGNATURE)][NumThreads(1, 1, 1)] void main(uint dtid : SV_DispatchThreadID) {
  if (dtid == 0) {
    DispatchIndirectCmd cmd;
    cmd.tg_x = 0;
    cmd.tg_y = 1;
    cmd.tg_z = 1;
    bindless_rwbuffers[buf_idx].Store<DispatchIndirectCmd>(0, cmd);
  }
}
