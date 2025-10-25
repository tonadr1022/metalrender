#pragma once

#include "Types.hpp"
#include "chunk_shaders_shared.h"
#include "core/EAssert.hpp"
#include "core/Logger.hpp"

namespace vox {

using VoxelId = uint8_t;

static constexpr int k_chunk_len_sq = k_chunk_len * k_chunk_len;
static constexpr int k_chunk_len_cu = k_chunk_len_sq * k_chunk_len;

static constexpr int k_chunk_len_padded = k_chunk_len + 2;
static constexpr int k_chunk_len_padded_sq = k_chunk_len_padded * k_chunk_len_padded;
static constexpr int k_chunk_len_padded_cu = k_chunk_len_padded_sq * k_chunk_len_padded;

inline int get_idx(int x, int y, int z) { return z + (x * k_chunk_len) + (y * k_chunk_len_sq); }
inline int get_idx(glm::ivec3 pos) { return get_idx(pos.x, pos.y, pos.z); }

inline int get_idx_2d(int x, int z) { return z + (x * k_chunk_len); }

constexpr int get_padded_idx(int x, int y, int z) {
  return z + (x * k_chunk_len_padded) + (y * k_chunk_len_padded_sq);
}

constexpr int get_padded_idx(glm::ivec3 pos) { return get_padded_idx(pos.x, pos.y, pos.z); }

constexpr glm::ivec3 chunk_pos_from_idx(int i) {
  return {i % k_chunk_len_padded, i / k_chunk_len_padded_sq,
          (i / k_chunk_len_padded) % k_chunk_len_padded};
}

inline int regular_idx_to_padded(int x, int y, int z) {
  return (z + 1) + ((x + 1) * k_chunk_len_padded) + ((y + 1) * k_chunk_len_padded_sq);
}

inline int regular_idx_to_padded(glm::ivec3 pos) {
  return regular_idx_to_padded(pos.x, pos.y, pos.z);
}

using MeshId = uint32_t;
struct ChunkVoxArr {
  std::array<VoxelId, k_chunk_len_cu> blocks;

  [[nodiscard]] VoxelId get(int x, int y, int z) const { return blocks[get_idx(x, y, z)]; }
  [[nodiscard]] VoxelId get(int i) const {
    ASSERT(i < (int)blocks.size());
    return blocks[i];
  }
  [[nodiscard]] VoxelId get(glm::ivec3 pos) const { return blocks[get_idx(pos)]; }
  void set(int x, int y, int z, VoxelId vox) { blocks[get_idx(x, y, z)] = vox; }
  void set(glm::ivec3 pos, VoxelId vox) { blocks[get_idx(pos)] = vox; }
  void set(int idx, VoxelId vox) { blocks[idx] = vox; }
};

struct ChunkLodData {
  std::array<uint32_t, 5> lods;
};

struct Chunk {
 private:
  ChunkVoxArr blocks;

 public:
  [[nodiscard]] const ChunkVoxArr& get_blocks() const { return blocks; }
  int non_air_block_count{};
  bool has_terrain{};
  bool has_mesh{};
  bool is_meshing{};

  constexpr static MeshId k_invalid_mesh_id = UINT32_MAX;

  [[nodiscard]] VoxelId get(int x, int y, int z) const { return blocks.get(x, y, z); }
  [[nodiscard]] VoxelId get(int i) const { return blocks.get(i); }
  [[nodiscard]] VoxelId get(glm::ivec3 pos) const { return blocks.get(pos); }
  void set(int x, int y, int z, VoxelId vox) { set(get_idx(x, y, z), vox); }
  void fill(VoxelId vox) {
    non_air_block_count = vox != 0 * k_chunk_len_cu;
    blocks.blocks.fill(vox);
  }
  void set(glm::ivec3 pos, VoxelId vox) { blocks.set(pos, vox); }
  void set(int idx, VoxelId vox) {
    non_air_block_count += -(blocks.get(idx) != 0) + vox != 0;
    blocks.set(idx, vox);
  }
};

using PaddedChunkVoxArr = std::vector<VoxelId>;

void populate_mesh(const PaddedChunkVoxArr& voxels, ChunkUploadData& result);

}  // namespace vox
