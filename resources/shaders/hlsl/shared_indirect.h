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

struct InstanceData {
  // uint32_t instance_id;
  // uint32_t mesh_id;
  // uint32_t meshlet_vis_base;
  packed_float3 translation;
  float scale;
  packed_float4 rotation;
  uint32_t mat_id;
};

// struct InstData {
//   float3 translation;
//   float4 rotation;
//   float scale;
//   uint material_id;
// };

#endif  // SHARED_INDIRECT_H
