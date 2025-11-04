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
  float4x4 model;
  // uint material_id;
  // uint base_vertex;
};

#endif  // SHARED_INDIRECT_H
