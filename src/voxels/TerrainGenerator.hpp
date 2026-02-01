#pragma once

#include <FastNoise/Generators/Fractal.h>
#include <FastNoise/SmartNode.h>

#include "core/Math.hpp"

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

namespace vox {

struct Chunk;

class TerrainGenerator {
 public:
  TerrainGenerator();
  void populate_chunk(glm::ivec3 chunk_key, Chunk& chunk);
  void generate_world_chunk(const glm::ivec3& chunk_key, Chunk& chunk);

 private:
  FastNoise::SmartNode<FastNoise::FractalFBm> fbm_noise_;
  uint64_t seed_{1};
};

}  // namespace vox

} // namespace TENG_NAMESPACE
