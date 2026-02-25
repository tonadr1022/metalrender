#ifndef SHARED_INSTANCE_DATA_H
#define SHARED_INSTANCE_DATA_H

#include "shader_core.h"

struct InstanceData {
  packed_float3 translation;
  float scale;
  glm_quat rotation;
  uint32_t mat_id;
  uint32_t mesh_id;
  uint32_t meshlet_vis_base;
};

#endif
