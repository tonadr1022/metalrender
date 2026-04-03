#include "shader_core.h"

struct BasicTriPC {
  float4x4 mvp;
  uint vert_buf_idx;
  uint mat_buf_idx;
  uint mat_buf_id;
};

PUSHCONSTANT(BasicTriPC, pc);