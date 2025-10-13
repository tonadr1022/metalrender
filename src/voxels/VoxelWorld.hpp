#pragma once

#include <unordered_map>

#include "voxels/Chunk.hpp"
#include "voxels/TerrainGenerator.hpp"

namespace vox {

class Renderer;

class VoxelWorld {
 public:
  void init() {
    glm::vec3 iter{};
    int radius = 2;
    for (iter.y = -radius; iter.y <= radius; iter.y++) {
      for (iter.x = -radius; iter.x <= radius; iter.x++) {
        for (iter.z = -radius; iter.z <= radius; iter.z++) {
          auto res = chunks_.emplace(iter, Chunk{});
          terrain_generator_.populate_chunk(res.first->first, res.first->second);
        }
      }
    }
  }

  void shutdown() {}

 private:
  Renderer* renderer_{};
  TerrainGenerator terrain_generator_;
  std::unordered_map<glm::vec3, Chunk> chunks_;
};

}  // namespace vox
