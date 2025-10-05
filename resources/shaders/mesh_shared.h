#ifndef MESH_SHARED_H
#define MESH_SHARED_H

#include "shader_core.h"

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

struct InstanceData {
  uint32_t instance_id;
  uint32_t mat_id;
  uint32_t mesh_id;
  packed_float3 translation;
  float scale;
  packed_float4 rotation;
};

struct CullData {
  float4x4 view;
  float frustum[4];
  bool meshlet_frustum_cull;
  float z_near;
  float z_far;
  packed_float3 camera_pos;
};

#endif
