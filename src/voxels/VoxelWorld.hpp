#pragma once

#include <queue>
#include <unordered_map>

#include "core/Handle.hpp"
#include "core/Pool.hpp"
#include "voxels/Chunk.hpp"
#include "voxels/Mesher.hpp"
#include "voxels/TerrainGenerator.hpp"
#include "voxels/VoxelDB.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "concurrentqueue.h"
#include "glm/gtx/hash.hpp"  // IWYU pragma: keep

class RendererMetal;

namespace vox {

class Renderer;

#define GREEDY_MESHING_ENABLED 1

void iterate_in_radius(glm::ivec3 iter, int radius, auto&& f) {
  auto end = iter + radius;
  auto begin = iter - radius;
  for (iter.y = begin.y; iter.y <= end.y; iter.y++) {
    for (iter.x = begin.x; iter.x <= end.x; iter.x++) {
      for (iter.z = begin.z; iter.z <= end.z; iter.z++) {
        f(iter);
      }
    }
  }
}

class World {
 public:
  void init(Renderer* renderer, RendererMetal* metal_renderer,
            const std::filesystem::path& resource_dir);
  void shutdown();
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
    return (z + 1) + ((x + 1) * 3) + ((y + 1) * 9);
  }

  static constexpr int get_nei_chunk_idx(glm::ivec3 key) {
    return get_nei_chunk_idx(key.y, key.y, key.z);
  }

  bool is_meshable(ChunkKey key) {
    for (int y = -1; y <= 1; y++) {
      for (int x = -1; x <= 1; x++) {
        for (int z = -1; z <= 1; z++) {
          Chunk* chunk = get(key + glm::ivec3(x, y, z));
          if (!chunk || !chunk->has_terrain) return false;
        }
      }
    }
    return true;
  }

  using NeiChunksArr = std::array<ChunkBlockArr, 27>;

  void fill_padded_chunk_blocks(const NeiChunksArr& nei_chunks, PaddedChunkVoxArr& result) const;
  void fill_padded_chunk_blocks_lod(const NeiChunksArr& nei_chunks, int lod,
                                    PaddedChunkVoxArr& result) const;

  // copy neighbor chunk data by value
  void fill_nei_chunks_block_arrays(ChunkKey key, NeiChunksArr& arr);
  void on_imgui();

 private:
  Renderer* renderer_{};
  RendererMetal* metal_renderer_{};
  std::unordered_map<ChunkKey, ChunkHandle> chunks_;
  BlockPool<ChunkHandle, Chunk> chunk_pool_{128, 10, false};
  inline static auto num_threads = std::thread::hardware_concurrency();

  using PaddedChunkVoxArrHandle = GenerationalHandle<PaddedChunkVoxArr>;
  BlockPool<PaddedChunkVoxArrHandle, PaddedChunkVoxArr> padded_chunk_voxel_arr_pool_{num_threads, 1,
                                                                                     false};
  using NeiChunksArrHandle = GenerationalHandle<NeiChunksArr>;
  BlockPool<NeiChunksArrHandle, NeiChunksArr> nei_chunks_arr_pool_{num_threads, 1, false};
  using MesherDataHandle = GenerationalHandle<greedy_mesher::MeshDataAllLods>;
  BlockPool<MesherDataHandle, greedy_mesher::MeshDataAllLods> mesher_data_pool_{num_threads, 1,
                                                                                false};

  ChunkHandle create_chunk(glm::ivec3 key) {
    auto handle = chunk_pool_.alloc();
    chunks_.emplace(key, handle);
    return handle;
  }
  // TODO: move
  TerrainGenerator terrain_generator_;
  struct TerrainGenTask {
    ChunkKey key;
    ChunkHandle handle;
  };
  std::queue<TerrainGenTask> to_terrain_gen_q_;
  moodycamel::ConcurrentQueue<ChunkUploadData> chunk_gpu_upload_q_;
  moodycamel::ConcurrentQueue<ChunkKey> ready_for_mesh_q_;
  size_t meshes_in_flight_{};
  std::atomic<size_t> terrain_tasks_in_flight_;
  size_t tasks_{};
  VoxelDB vdb_;
  std::filesystem::path resource_dir_;
  std::filesystem::path vdb_blocks_path_;

  void send_chunk_task(ChunkKey key);
};

}  // namespace vox
