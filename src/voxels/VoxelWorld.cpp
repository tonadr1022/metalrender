#include "VoxelWorld.hpp"

#include <tracy/Tracy.hpp>

#include "voxels/Chunk.hpp"
#include "voxels/TerrainGenerator.hpp"
#include "voxels/VoxelRenderer.hpp"

namespace vox {

void World::init(Renderer* renderer) {
  ZoneScoped;
  renderer_ = renderer;
  auto nei_chunks = std::make_unique<NeiChunksArr>();
  {
    glm::vec3 key{};
    int radius = 3;
    for (key.y = -radius; key.y <= radius; key.y++) {
      for (key.x = -radius; key.x <= radius; key.x++) {
        for (key.z = -radius; key.z <= radius; key.z++) {
          ChunkHandle chunk_handle = create_chunk(key);
          Chunk& chunk = *get(chunk_handle);
          terrain_generator_.generate_world_chunk(key, chunk);
          ready_for_mesh_queue_.emplace(key);
        }
      }
    }
  }
}

void World::fill_padded_chunk_blocks(const NeiChunksArr& nei_chunks,
                                     PaddedChunkVoxArr& result) const {
  ZoneScoped;
  auto get_chunk = [&nei_chunks](int x, int y, int z) -> const ChunkVoxArr& {
    return nei_chunks[get_nei_chunk_idx(x, y, z)];
  };
  const ChunkVoxArr& main_chunk = get_chunk(0, 0, 0);

  for (int y = 1; y <= k_chunk_len; y++) {
    for (int z = 1; z <= k_chunk_len; z++) {
      for (int x = 1; x <= k_chunk_len; x++) {
        result[get_padded_idx(x, y, z)] = main_chunk.get(x - 1, y - 1, z - 1);
      }
    }
  }

  // 8 corners
  constexpr glm::ivec3 corner_chunk_offsets[] = {
      glm::ivec3{-1, -1, -1}, glm::ivec3{-1, -1, 1}, glm::ivec3{-1, 1, -1}, glm::ivec3{-1, 1, 1},
      glm::ivec3{1, -1, -1},  glm::ivec3{1, -1, 1},  glm::ivec3{1, 1, -1},  glm::ivec3{1, 1, 1},
  };

  auto chunk_edge_to_chunk_pos = [](const glm::ivec3& p) -> glm::ivec3 {
    return {
        p.x == -1 ? k_chunk_len - 1 : 0,
        p.y == -1 ? k_chunk_len - 1 : 0,
        p.z == -1 ? k_chunk_len - 1 : 0,
    };
  };

  for (const glm::ivec3& c_off : corner_chunk_offsets) {
    result[get_padded_idx(c_off.x == -1 ? 0 : k_chunk_len + 1, c_off.y == -1 ? 0 : k_chunk_len + 1,
                          c_off.z == -1 ? 0 : k_chunk_len + 1)] =
        get_chunk(c_off.x, c_off.y, c_off.z).get(chunk_edge_to_chunk_pos(c_off));
  }

  {  // x constant
    const auto& pos_x_chunk = get_chunk(1, 0, 0);
    const auto& neg_x_chunk = get_chunk(-1, 0, 0);
    for (int y = 1; y <= k_chunk_len; y++) {
      for (int z = 1; z <= k_chunk_len; z++) {
        result[get_padded_idx(k_chunk_len + 1, y, z)] = pos_x_chunk.get(0, y - 1, z - 1);
        result[get_padded_idx(0, y, z)] = neg_x_chunk.get(k_chunk_len - 1, y - 1, z - 1);
      }
    }
  }
  {  // y constant
    const auto& pos_y_chunk = get_chunk(0, 1, 0);
    const auto& neg_y_chunk = get_chunk(0, -1, 0);
    for (int z = 1; z <= k_chunk_len; z++) {
      for (int x = 1; x <= k_chunk_len; x++) {
        result[get_padded_idx(x, k_chunk_len + 1, z)] = pos_y_chunk.get(x - 1, 0, z - 1);
        result[get_padded_idx(x, 0, z)] = neg_y_chunk.get(x - 2, k_chunk_len - 1, z - 1);
      }
    }
  }
  {  // z constant
    const auto& pos_z_chunk = get_chunk(0, 0, 1);
    const auto& neg_z_chunk = get_chunk(0, 0, -1);
    for (int y = 1; y <= k_chunk_len; y++) {
      for (int x = 1; x <= k_chunk_len; x++) {
        result[get_padded_idx(x, y, k_chunk_len + 1)] = pos_z_chunk.get(x - 1, y - 1, 0);
        result[get_padded_idx(x, y, 0)] = neg_z_chunk.get(x - 1, y - 1, k_chunk_len - 1);
      }
    }
  }

  {  // xz constant
    for (int y = 1; y <= k_chunk_len; y++) {
      // pos xz edge
      result[get_padded_idx(k_chunk_len + 1, y, k_chunk_len + 1)] =
          get_chunk(1, 0, 1).get(0, y - 1, 0);
      // neg xz edge
      result[get_padded_idx(0, y, 0)] =
          get_chunk(-1, 0, -1).get(k_chunk_len - 1, y - 1, k_chunk_len - 1);
      // pos x neg z
      result[get_padded_idx(k_chunk_len + 1, y, 0)] =
          get_chunk(1, 0, -1).get(0, y - 1, k_chunk_len - 1);
      // neg x pos z
      result[get_padded_idx(0, y, k_chunk_len + 1)] =
          get_chunk(-1, 0, 1).get(k_chunk_len - 1, y - 1, 0);
    }
  }

  {  // xy constant
    for (int z = 1; z <= k_chunk_len; z++) {
      // pos xy edge
      result[get_padded_idx(k_chunk_len + 1, k_chunk_len + 1, z)] =
          get_chunk(1, 1, 0).get(0, 0, z - 1);
      // neg xy edge
      result[get_padded_idx(0, 0, z)] =
          get_chunk(-1, -1, 0).get(k_chunk_len - 1, k_chunk_len - 1, z - 1);
      // pos x neg y
      result[get_padded_idx(k_chunk_len + 1, 0, z)] =
          get_chunk(1, -1, 0).get(0, k_chunk_len - 1, z - 1);
      // neg x pos y
      result[get_padded_idx(0, k_chunk_len + 1, z)] =
          get_chunk(-1, 1, 0).get(k_chunk_len - 1, 0, z - 1);
    }
  }

  {  // yz constant
    for (int x = 1; x <= k_chunk_len; x++) {
      // pos yz edge
      result[get_padded_idx(x, k_chunk_len + 1, k_chunk_len + 1)] =
          get_chunk(0, 1, 1).get(x - 1, 0, 0);
      // neg yz edge
      result[get_padded_idx(x, 0, 0)] =
          get_chunk(0, -1, -1).get(x - 1, k_chunk_len - 1, k_chunk_len - 1);
      // pos y neg z
      result[get_padded_idx(x, k_chunk_len + 1, 0)] =
          get_chunk(0, 1, -1).get(x - 1, 0, k_chunk_len - 1);
      // neg y pos z
      result[get_padded_idx(x, 0, k_chunk_len + 1)] =
          get_chunk(0, -1, 1).get(x - 1, k_chunk_len - 1, 0);
    }
  }
}

void World::fill_nei_chunks_block_arrays(ChunkKey key, NeiChunksArr& arr) {
  ZoneScoped;
  for (int y = -1, i = 0; y <= 1; y++) {
    for (int z = -1; z <= 1; z++) {
      for (int x = -1; x <= 1; x++, i++) {
        arr[i] = get(key + glm::ivec3{x, y, z})->blocks;
      }
    }
  }
}

void World::update(float) {
  ZoneScoped;
  if (!nei_chunks_tmp_) {
    nei_chunks_tmp_ = std::make_unique<NeiChunksArr>();
  }
  while (!ready_for_mesh_queue_.empty()) {
    auto key = ready_for_mesh_queue_.front();
    ready_for_mesh_queue_.pop();
    if (!is_meshable(key)) {
      continue;
    }

    auto handle = get_handle(key);
    if (!handle.is_valid()) {
      continue;
    }
    fill_nei_chunks_block_arrays(key, *nei_chunks_tmp_);

    std::unique_ptr<PaddedChunkVoxArr> padded_chunk_blocks = std::make_unique<PaddedChunkVoxArr>();

    fill_padded_chunk_blocks(*nei_chunks_tmp_, *padded_chunk_blocks);

    // TODO: race condition with get();
    renderer_->upload_chunk(handle, key, *get(handle), *padded_chunk_blocks);
  }
}

}  // namespace vox
