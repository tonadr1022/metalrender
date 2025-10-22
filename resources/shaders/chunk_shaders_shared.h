#ifndef CHUNK_SHADERS_SHARED_H
#define CHUNK_SHADERS_SHARED_H

#include "shader_core.h"

#define k_chunk_len 62

struct PerChunkUniforms {
  int4 chunk_pos;
};

struct VoxelMaterial {
  packed_float3 color;
};

#endif
