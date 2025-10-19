#pragma once

#include "chunk_shaders_shared.h"
#include "core/Handle.hpp"
#include "core/Math.hpp"

class Chunk;
using ChunkKey = glm::ivec3;
using ChunkHandle = GenerationalHandle<Chunk>;

struct ChunkUploadData {
  ChunkKey key;
  ChunkHandle handle;
  std::vector<VoxelVertex> vertices;
  uint32_t index_count;
  uint32_t vertex_count;
};
