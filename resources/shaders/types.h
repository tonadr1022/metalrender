#ifndef TYPES_H
#define TYPES_H

#include "shader_core.h"

struct GPUMeshData {
  uint32_t vertex_offset;
  uint32_t vertex_count;
};

struct GPUInstanceData {
  uint32_t mesh_id;
};

#endif
