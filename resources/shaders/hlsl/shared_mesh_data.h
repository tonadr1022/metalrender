#ifndef SHARED_MESH_DATA_H
#define SHARED_MESH_DATA_H

#include "../shader_core.h"

struct MeshData {
  uint32_t meshlet_base;
  uint32_t meshlet_count;
  uint32_t meshlet_vertices_offset;
  uint32_t meshlet_triangles_offset;
  uint32_t index_count;
  uint32_t index_offset;
  uint32_t vertex_base;
  uint32_t vertex_count;
  // bounding sphere
  packed_float3 center;
  float radius;
};

#endif
