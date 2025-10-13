#pragma once

#include <span>

#include "Types.hpp"
#include "voxels/Chunk.hpp"

namespace vox {

class Renderer {
 public:
  void init() {
    indices_.reserve(static_cast<size_t>(k_chunk_len_cu) * 6 * 6);
    for (int i = 0; i < k_chunk_len_cu * 6; i++) {
      size_t start_idx = indices_.size();
      indices_.emplace_back(start_idx + 0);
      indices_.emplace_back(start_idx + 1);
      indices_.emplace_back(start_idx + 2);
      indices_.emplace_back(start_idx + 1);
      indices_.emplace_back(start_idx + 3);
      indices_.emplace_back(start_idx + 2);
    }
  }
  void upload_chunk(std::span<VoxelVertex> vertices, std::span<uint32_t> indices);

 private:
  std::vector<uint32_t> indices_;  // todo: move
};

}  // namespace vox
