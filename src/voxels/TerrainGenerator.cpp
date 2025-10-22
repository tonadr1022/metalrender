#include "TerrainGenerator.hpp"

#include <FastNoise/FastNoise.h>

#include "core/Logger.hpp"
#include "voxels/Chunk.hpp"

namespace vox {

void TerrainGenerator::populate_chunk([[maybe_unused]] glm::ivec3 chunk_key, Chunk& chunk) {
  chunk.fill(0);
  if (chunk_key != glm::ivec3{0, 0, 0}) {
    return;
  }
  int n = k_chunk_len - 2;
  for (int y = 0; y < n; y++) {
    for (int z = 0; z < n; z++) {
      for (int x = 0; x < n; x++) {
        chunk.set(x, y, z, 1);
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
  // populate_chunk(chunk_key, chunk);
  // return;
  // TODO: pool
  std::vector<float> noise;
  noise.resize(k_chunk_len_sq);
  const glm::ivec3 chunk_world_pos_i = chunk_key * glm::ivec3{k_chunk_len};
  const auto chunk_world_pos = glm::vec3{chunk_world_pos_i};
  int non_air_count = 0;
  fbm_noise_->GenUniformGrid2D(noise.data(), chunk_world_pos_i.x, chunk_world_pos_i.z, k_chunk_len,
                               k_chunk_len, 0.01, seed_);
  constexpr int k_world_height = k_chunk_len;
  for (int y = 0, i = 0; y < k_chunk_len; y++) {
    for (int x = 0; x < k_chunk_len; x++) {
      for (int z = 0; z < k_chunk_len; z++, i++) {
        float world_y = chunk_world_pos.y + y;
        int noise_height = (noise[get_idx_2d(z, x)] * .5 + .5) * k_world_height;
        if (world_y < noise_height) {
          uint32_t color{};
          auto diff = noise_height - world_y;
          if (diff == 1) {
            color = 1;
          } else if (diff == 2) {
            color = 2;
          } else {
            color = 3;
          }
          chunk.set(i, color);
          non_air_count += color != 0;
        }
      }
    }
  }
  chunk.non_air_block_count = non_air_count;
}

}  // namespace vox
