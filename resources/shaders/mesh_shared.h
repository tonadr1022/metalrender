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
  uint32_t meshlet_vis_base;
  packed_float3 translation;
  float scale;
  packed_float4 rotation;
};

struct CullData {
  float4x4 view;
  float4x4 proj;
  packed_float3 camera_pos;
  float frustum[4];
  float z_near;
  float z_far;
  float p00;
  float p11;
  uint32_t pyramid_width;
  uint32_t pyramid_height;
  uint32_t pyramid_mip_count;
  uint32_t pad;
  bool meshlet_occlusion_culling_enabled;
  bool meshlet_frustum_cull;
  bool paused;
  uint8_t _pad[1];
};

enum MainObjectArgs {
  MainObjectArgs_MeshDataBuf,
  MainObjectArgs_MeshletBuf,
  MainObjectArgs_MeshletVisBuf,
  MainObjectArgs_DepthPyramidTex,
};

#endif
