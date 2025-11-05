#ifndef SHARED_INDIRECT_H
#define SHARED_INDIRECT_H

#include "../shader_core.h"

struct IndexedIndirectDrawCmd {
  uint32_t index_count;
  uint32_t instance_count;
  uint32_t first_index;
  int32_t vertex_offset;
  uint32_t first_instance;
};

struct InstData {
  float3 translation;
  float4 rotation;
  float scale;
  uint material_id;
};

#endif  // SHARED_INDIRECT_H
