#ifndef DEFAULT_VERTEX_H
#define DEFAULT_VERTEX_H

#include "shader_core.h"

struct DefaultVertex {
  packed_float4 pos;
  packed_float2 uv;
  packed_float3 normal;
};

struct MeshletVertex {
  float4 pos ATTR_POSITION;
  float2 uv;
  float3 normal;
};

// see meshoptimizer.h
struct Meshlet {
  uint32_t vertex_offset;
  uint32_t triangle_offset;
  uint32_t vertex_count;
  uint32_t triangle_count;

  // bounding sphere
  packed_float3 center;
  float radius;

  // normal cone
  packed_float3 cone_apex;
  packed_float3 cone_axis;
  float cone_cutoff;
};

#endif
