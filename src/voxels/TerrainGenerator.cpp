#include "TerrainGenerator.hpp"

#include "voxels/Chunk.hpp"
namespace vox {

void TerrainGenerator::populate_chunk([[maybe_unused]] glm::ivec3 chunk_key, Chunk& chunk) {
  for (int y = 0; y < k_chunk_len; y++) {
    for (int z = 0; z < k_chunk_len; z++) {
      for (int x = 0; x < k_chunk_len; x++) {
        if (y % 3 != 0 && x % 3 != 0) {
          chunk.blocks[get_idx(x, y, z)] = 1;
        }
      }
    }
  }
}

}  // namespace vox
