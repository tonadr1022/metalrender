#pragma once

#include "Types.hpp"

namespace vox {

using VoxelId = uint32_t;

static constexpr int k_chunk_len = 64;
static constexpr int k_chunk_len_sq = k_chunk_len * k_chunk_len;
static constexpr int k_chunk_len_cu = k_chunk_len_sq * k_chunk_len;

static constexpr int k_chunk_len_padded = 66;
static constexpr int k_chunk_len_padded_sq = k_chunk_len_padded;
static constexpr int k_chunk_len_padded_cu = k_chunk_len_padded_sq * k_chunk_len_padded;

inline int get_idx(int x, int y, int z) { return x + (z * k_chunk_len) + (y * k_chunk_len_sq); }
inline int get_idx(glm::ivec3 pos) {
  return pos.x + (pos.z * k_chunk_len) + (pos.y * k_chunk_len_sq);
}

using MeshId = uint32_t;
struct Chunk {
  std::array<VoxelId, k_chunk_len_cu> blocks;
  MeshId mesh_id;
  MeshId k_invalid_mesh_id = UINT32_MAX;
};

using PaddedChunkVoxArr = std::array<VoxelId, k_chunk_len_padded_cu>;

void populate_mesh(const PaddedChunkVoxArr& voxels, std::vector<VoxelVertex>& out_vertices);

}  // namespace vox
