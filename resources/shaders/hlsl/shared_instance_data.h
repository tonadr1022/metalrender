#ifndef SHARED_INSTANCE_DATA_H
#define SHARED_INSTANCE_DATA_H

#include "../shader_core.h"

struct InstanceData {
  packed_float3 translation;
  float scale;
  packed_float4 rotation;
  uint32_t mat_id;
  uint32_t mesh_id;
};

#endif
