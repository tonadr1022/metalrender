#pragma once

#include "chunk_shaders_shared.h"
#include "core/Handle.hpp"

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

class Chunk;
using ChunkKey = glm::ivec3;
using ChunkHandle = GenerationalHandle<Chunk>;

struct ChunkUploadData {
  ChunkKey key;
  ChunkHandle handle;
  struct PerLod {
    std::vector<uint64_t> vertices;
    uint32_t quad_count;
    std::array<uint32_t, 6> face_vert_begin{};
    std::array<uint32_t, 6> face_vert_length{};
  };

  std::array<PerLod, 6> lods{};
};

} // namespace TENG_NAMESPACE
