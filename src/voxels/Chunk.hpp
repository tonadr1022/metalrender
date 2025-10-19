#pragma once

#include "Types.hpp"
#include "chunk_shaders_shared.h"
#include "core/EAssert.hpp"

namespace vox {

using VoxelId = uint32_t;

static constexpr int k_chunk_len_sq = k_chunk_len * k_chunk_len;
static constexpr int k_chunk_len_cu = k_chunk_len_sq * k_chunk_len;

static constexpr int k_chunk_len_padded = k_chunk_len + 2;
static constexpr int k_chunk_len_padded_sq = k_chunk_len_padded * k_chunk_len_padded;
static constexpr int k_chunk_len_padded_cu = k_chunk_len_padded_sq * k_chunk_len_padded;

inline int get_idx(int x, int y, int z) { return x + (z * k_chunk_len) + (y * k_chunk_len_sq); }
inline int get_idx(glm::ivec3 pos) {
  return pos.x + (pos.z * k_chunk_len) + (pos.y * k_chunk_len_sq);
}

inline int get_idx_2d(int x, int z) { return x + (z * k_chunk_len); }

constexpr int get_padded_idx(int x, int y, int z) {
  return (x) + ((z)*k_chunk_len_padded) + ((y)*k_chunk_len_padded_sq);
}

constexpr int get_padded_idx(glm::ivec3 pos) { return get_padded_idx(pos.x, pos.y, pos.z); }

constexpr glm::ivec3 chunk_pos_from_idx(int i) {
  return {i % k_chunk_len_padded, i / k_chunk_len_padded_sq,
          (i / k_chunk_len_padded) % k_chunk_len_padded};
}

inline int regular_idx_to_padded(int x, int y, int z) {
  return (x + 1) + ((z + 1) * k_chunk_len_padded) + ((y + 1) * k_chunk_len_padded_sq);
}

inline int regular_idx_to_padded(glm::ivec3 pos) {
  return (pos.x + 1) + ((pos.z + 1) * k_chunk_len_padded) + ((pos.y + 1) * k_chunk_len_padded_sq);
}

using MeshId = uint32_t;
struct ChunkVoxArr {
  std::array<VoxelId, k_chunk_len_cu> blocks;

  [[nodiscard]] VoxelId get(int x, int y, int z) const {
    ASSERT(get_idx(x, y, z) < k_chunk_len_cu);
    return blocks[get_idx(x, y, z)];
  }
  [[nodiscard]] VoxelId get(glm::ivec3 pos) const { return blocks[get_idx(pos)]; }
  void set(int x, int y, int z, VoxelId vox) { blocks[get_idx(x, y, z)] = vox; }
  void set(glm::ivec3 pos, VoxelId vox) { blocks[get_idx(pos)] = vox; }
  void set(int idx, VoxelId vox) { blocks[idx] = vox; }
};

struct Chunk {
  ChunkVoxArr blocks;
  MeshId mesh_id;
  MeshId k_invalid_mesh_id = UINT32_MAX;

  [[nodiscard]] VoxelId get(int x, int y, int z) const { return blocks.get(x, y, z); }
  [[nodiscard]] VoxelId get(glm::ivec3 pos) const { return blocks.get(pos); }
  void set(int x, int y, int z, VoxelId vox) { blocks.set(x, y, z, vox); }
  void set(glm::ivec3 pos, VoxelId vox) { blocks.set(pos, vox); }
  void set(int idx, VoxelId vox) { blocks.set(idx, vox); }
};

using PaddedChunkVoxArr = std::vector<VoxelId>;

void populate_mesh(const PaddedChunkVoxArr& voxels, ChunkUploadData& result);

}  // namespace vox
