// clang-format off
#define COMPUTE_ROOT_SIG
#include "root_sig.h"
#include "shared_test_clear_buf.h"
// clang-format on

struct DispatchIndirectCmd {
  uint tg_x;
  uint tg_y;
  uint tg_z;
};

[RootSignature(ROOT_SIGNATURE)][NumThreads(64, 1, 1)] void main(uint dtid : SV_DispatchThreadID) {
  RWStructuredBuffer<DispatchIndirectCmd> buf = ResourceDescriptorHeap[buf_idx];
  if (dtid == 0) {
    buf[0].tg_x = 0;
    buf[0].tg_y = 1;
    buf[0].tg_z = 1;
  }
}
