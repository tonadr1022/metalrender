#ifndef SHARED_TEST_CLEAR_BUF_H
#define SHARED_TEST_CLEAR_BUF_H

#include "shader_core.h"

struct TestClearBufPC {
  uint buf_idx;
};

PUSHCONSTANT(TestClearBufPC, pc);

#endif
