#ifndef MESH_SHARED_H
#define MESH_SHARED_H

#include "shader_core.h"

struct MeshData {
  uint32_t meshlet_base;
  uint32_t meshlet_count;
  uint32_t meshlet_vertices_offset;
  uint32_t meshlet_triangles_offset;
};

struct InstanceData {
  uint32_t instance_id;
  uint32_t mat_id;
  uint32_t mesh_id;
};

struct CullData {
  float4x4 view;
  float frustum[4];
  bool meshlet_frustum_cull;
};

#endif
