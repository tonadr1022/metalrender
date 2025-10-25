#ifndef CHUNK_SHADERS_SHARED_H
#define CHUNK_SHADERS_SHARED_H

#include "shader_core.h"

#define k_chunk_len 32
#define k_chunk_bits 5

struct PerChunkUniforms {
  int4 chunk_pos;
};

struct VoxelMaterial {
  uint indices[12];
};

struct VoxelFragmentUniforms {
  bool normal_map_enabled;
};

#define k_invalid_idx 4294967295  // UINT32_MAX

#endif
