#pragma once

#include <queue>
#include <unordered_map>

#include "core/Pool.hpp"
#include "voxels/Chunk.hpp"
#include "voxels/TerrainGenerator.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/hash.hpp"  // IWYU pragma: keep

namespace vox {

class Renderer;

class World {
 public:
  void init(Renderer* renderer);
  void update(float dt);

  ChunkHandle get_handle(ChunkKey key) {
    auto it = chunks_.find(key);
    return it == chunks_.end() ? ChunkHandle{} : it->second;
  }

  Chunk* get(ChunkKey key) {
    auto it = chunks_.find(key);
    return it == chunks_.end() ? nullptr : chunk_pool_.get(it->second);
  }

  [[nodiscard]] const Chunk* get(ChunkKey key) const {
    auto it = chunks_.find(key);
    return it == chunks_.end() ? nullptr : chunk_pool_.get(it->second);
  }

  Chunk* get(ChunkHandle handle) { return chunk_pool_.get(handle); }

  static constexpr int get_nei_chunk_idx(int x, int y, int z) {
    return (x + 1) + ((z + 1) * 3) + ((y + 1) * 9);
  }

  static constexpr int get_nei_chunk_idx(glm::ivec3 key) {
    return get_nei_chunk_idx(key.x, key.y, key.z);
  }

  bool is_meshable(ChunkKey key) {
    for (int y = -1; y <= 1; y++) {
      for (int z = -1; z <= 1; z++) {
        for (int x = -1; x <= 1; x++) {
          if (!get(key + glm::ivec3(x, y, z))) return false;
        }
      }
    }
    return true;
  }

  using NeiChunksArr = std::array<ChunkVoxArr, 27>;
  void fill_padded_chunk_blocks(const NeiChunksArr& nei_chunks, PaddedChunkVoxArr& result) const;

  // copy neighbor chunk data by value
  void fill_nei_chunks_block_arrays(ChunkKey key, NeiChunksArr& arr);

 private:
  Renderer* renderer_{};
  std::unordered_map<ChunkKey, ChunkHandle> chunks_;
  BlockPool<ChunkHandle, Chunk> chunk_pool_;

  ChunkHandle create_chunk(glm::ivec3 key) {
    auto handle = chunk_pool_.alloc();
    chunks_.emplace(key, handle);
    return handle;
  }
  std::queue<ChunkKey> ready_for_mesh_queue_;
  // TODO: move
  std::unique_ptr<NeiChunksArr> nei_chunks_tmp_;
  TerrainGenerator terrain_generator_;
};

}  // namespace vox
