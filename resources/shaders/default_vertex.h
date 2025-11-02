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

  // stored in 8-bit SNORM format
  packed_char4 cone_axis_cutoff;
};

#endif
