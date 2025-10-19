#include "TerrainGenerator.hpp"

#include <FastNoise/FastNoise.h>

#include "voxels/Chunk.hpp"

namespace vox {

void TerrainGenerator::populate_chunk([[maybe_unused]] glm::ivec3 chunk_key, Chunk& chunk) {
  if (chunk_key != glm::ivec3{0, 0, 0}) {
    return;
  }
  for (int y = 0; y < k_chunk_len; y++) {
    for (int z = 0; z < k_chunk_len; z++) {
      for (int x = 0; x < k_chunk_len; x++) {
        if (x % 2 != 0 && z % 2 != 0 && y % 2 == 0) {
          chunk.set(x, y, z, 1);
        }
      }
    }
  }
}

TerrainGenerator::TerrainGenerator() {
  auto fn_simplex = FastNoise::New<FastNoise::Simplex>();
  fbm_noise_ = FastNoise::New<FastNoise::FractalFBm>();
  fbm_noise_->SetSource(fn_simplex);
  fbm_noise_->SetOctaveCount(6);
}

void TerrainGenerator::generate_world_chunk(const glm::ivec3& chunk_key, Chunk& chunk) {
  std::array<float, k_chunk_len_sq> noise;
  const glm::ivec3 chunk_world_pos_i = chunk_key * glm::ivec3{k_chunk_len};
  const auto chunk_world_pos = glm::vec3{chunk_world_pos_i};
  fbm_noise_->GenUniformGrid2D(noise.data(), chunk_world_pos_i.x, chunk_world_pos_i.z, k_chunk_len,
                               k_chunk_len, 0.01, seed_);
  constexpr int k_world_height = 64;
  for (int y = 0, i = 0; y < k_chunk_len; y++) {
    for (int z = 0; z < k_chunk_len; z++) {
      for (int x = 0; x < k_chunk_len; x++, i++) {
        float world_y = chunk_world_pos.y + y;
        if (world_y < (noise[get_idx_2d(x, z)] * .5 + .5) * k_world_height) {
          chunk.set(i, 1);
        }
      }
    }
  }
}

}  // namespace vox
