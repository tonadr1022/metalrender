#ifndef CHUNK_SHADERS_SHARED_H
#define CHUNK_SHADERS_SHARED_H

#include "shader_core.h"

#define k_chunk_len 64

struct PerChunkUniforms {
  float3 chunk_pos;
};

struct VoxelVertex {
  float4 pos;
};

#endif
