#define COMPUTE_ROOT_SIG
#include "root_sig.h"
#include "shared_draw_cull.h"

struct MeshDraw {
  uint idx;
  uint dtid;
};

// TODO: evaluate threads per threadgroup
[RootSignature(ROOT_SIGNATURE)][NumThreads(64, 1, 1)] void main(uint dtid : SV_DispatchThreadID) {
  RWStructuredBuffer<uint> draw_cnt_buf = ResourceDescriptorHeap[out_draw_cnt_buf_idx];
  uint idx;
  InterlockedAdd(draw_cnt_buf[0], 1, idx);
  RWStructuredBuffer<MeshDraw> test_draw_buf = ResourceDescriptorHeap[test_buf_idx];
  if (dtid == 0) {
    test_draw_buf[0].dtid = frame_num;
    test_draw_buf[0].idx = frame_num;
  } else {
    uint numElements;
    uint stride;
    test_draw_buf.GetDimensions(numElements, stride);

    if (dtid < numElements) {
      test_draw_buf[dtid].dtid = dtid;
      test_draw_buf[dtid].idx += idx;
    }
  }
}
