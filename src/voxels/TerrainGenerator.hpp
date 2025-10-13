#pragma once

#include "core/Math.hpp"

struct Chunk;

namespace vox {

class TerrainGenerator {
 public:
  void populate_chunk(glm::ivec3 chunk_key, Chunk& chunk);

 private:
};

}  // namespace vox
