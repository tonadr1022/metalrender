#ifndef SHARED_TASK_CMD_H
#define SHARED_TASK_CMD_H

#include "shader_core.h"  // IWYU pragma: keep

struct TaskCmd {
  uint32_t instance_id;
  uint32_t task_offset;
  uint32_t task_count;
};

#endif
