#include "VoxelWorld.hpp"

#include <tracy/Tracy.hpp>

#include "core/EAssert.hpp"
#include "core/Logger.hpp"
#include "core/ThreadPool.hpp"
#include "voxels/Chunk.hpp"
#include "voxels/TerrainGenerator.hpp"
#include "voxels/Types.hpp"
#include "voxels/VoxelRenderer.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#define BM_IMPEMENTATION
#include "Mesher.hpp"

namespace {

constexpr int radius = 10;
constexpr glm::ivec2 y_chunk_range = {-1, 2};

}  // namespace
namespace vox {

void World::init(Renderer* renderer) {
  ZoneScoped;
  renderer_ = renderer;
  {
    glm::vec3 key{};
    for (key.y = y_chunk_range.x; key.y <= y_chunk_range.y; key.y++) {
      for (key.x = -radius; key.x <= radius; key.x++) {
        for (key.z = -radius; key.z <= radius; key.z++) {
          to_terrain_gen_q_.emplace(key, create_chunk(key));
        }
      }
    }
  }
}

void World::fill_padded_chunk_blocks(const NeiChunksArr& nei_chunks,
                                     PaddedChunkVoxArr& result) const {
  result.resize(k_chunk_len_padded_cu);
  ZoneScoped;
  auto get_chunk = [&nei_chunks](int x, int y, int z) -> const ChunkVoxArr& {
    return nei_chunks[get_nei_chunk_idx(x, y, z)];
  };
  const ChunkVoxArr& main_chunk = get_chunk(0, 0, 0);

  for (int y = 1; y <= k_chunk_len; y++) {
    for (int x = 1; x <= k_chunk_len; x++) {
      for (int z = 1; z <= k_chunk_len; z++) {
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
        result[get_padded_idx(k_chunk_len_padded - 1, y, z)] = pos_x_chunk.get(0, y - 1, z - 1);
        result[get_padded_idx(0, y, z)] = neg_x_chunk.get(k_chunk_len - 1, y - 1, z - 1);
      }
    }
  }
  {  // y constant
    const auto& pos_y_chunk = get_chunk(0, 1, 0);
    const auto& neg_y_chunk = get_chunk(0, -1, 0);
    for (int z = 1; z <= k_chunk_len; z++) {
      for (int x = 1; x <= k_chunk_len; x++) {
        result[get_padded_idx(x, k_chunk_len_padded - 1, z)] = pos_y_chunk.get(x - 1, 0, z - 1);
        result[get_padded_idx(x, 0, z)] = neg_y_chunk.get(x - 1, k_chunk_len - 1, z - 1);
      }
    }
  }
  {  // z constant
    const auto& pos_z_chunk = get_chunk(0, 0, 1);
    const auto& neg_z_chunk = get_chunk(0, 0, -1);
    for (int y = 1; y <= k_chunk_len; y++) {
      for (int x = 1; x <= k_chunk_len; x++) {
        result[get_padded_idx(x, y, k_chunk_len_padded - 1)] = pos_z_chunk.get(x - 1, y - 1, 0);
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
  ASSERT(is_meshable(key));
  for (int y = -1, i = 0; y <= 1; y++) {
    for (int x = -1; x <= 1; x++) {
      for (int z = -1; z <= 1; z++, i++) {
        arr[i] = get(key + glm::ivec3{x, y, z})->get_blocks();
      }
    }
  }
}

void World::update(float) {
  ZoneScoped;
  {
    while (!to_terrain_gen_q_.empty() && terrain_tasks_in_flight_.load() < 32) {
      TerrainGenTask terrain_task = to_terrain_gen_q_.front();
      to_terrain_gen_q_.pop();
      terrain_tasks_in_flight_++;
      ThreadPool::get().detach_task([this, terrain_task]() {
        Chunk* chunk = get(terrain_task.handle);
        ChunkKey key = terrain_task.key;
        if (chunk) {
          terrain_generator_.generate_world_chunk(key, *chunk);
          if (chunk->non_air_block_count) {
            ready_for_mesh_q_.enqueue(key);
          }
        }
        terrain_tasks_in_flight_--;
      });
    }
  }

  {
    auto pos = glm::ivec3{};
    auto begin = pos + glm::ivec3{-radius, y_chunk_range.x, -radius};
    auto end = pos + glm::ivec3{radius, y_chunk_range.y, radius};
    glm::ivec3 iter;
    for (iter.y = begin.y; iter.y <= end.y; iter.y++) {
      for (iter.x = begin.x; iter.x <= end.x; iter.x++) {
        for (iter.z = begin.z; iter.z <= end.z; iter.z++) {
          if (meshes_in_flight_ > 32) {
            break;
          }
          Chunk* chunk = get(iter);
          ASSERT(chunk);
          if (chunk->has_terrain && chunk->non_air_block_count > 0 && !chunk->has_mesh &&
              !chunk->is_meshing && is_meshable(iter)) {
            chunk->is_meshing = true;
            send_chunk_task(iter);
          }
        }
      }
    }
  }

  {
    ChunkUploadData gpu_upload_data;
    while (chunk_gpu_upload_q_.try_dequeue(gpu_upload_data)) {
      meshes_in_flight_--;
      // TODO: refactor
      renderer_->upload_chunk(gpu_upload_data, gpu_upload_data.vertices);
    }
  }
}

void World::send_chunk_task(ChunkKey key) {
  auto handle = get_handle(key);
  if (!handle.is_valid()) {
    return;
  }

  const NeiChunksArrHandle nei_chunk_arr_handle = nei_chunks_arr_pool_.alloc();
  fill_nei_chunks_block_arrays(key, *nei_chunks_arr_pool_.get(nei_chunk_arr_handle));

  meshes_in_flight_++;

  ThreadPool::get().detach_task([this, key, handle, nei_chunk_arr_handle]() {
    // TODO: vertices are malloced here
    ChunkUploadData gpu_upload_data{
        .key = key,
        .handle = handle,
    };
    const PaddedChunkVoxArrHandle padded_chunk_block_handle = padded_chunk_voxel_arr_pool_.alloc();
    PaddedChunkVoxArr& padded_blocks = *padded_chunk_voxel_arr_pool_.get(padded_chunk_block_handle);
    fill_padded_chunk_blocks(*nei_chunks_arr_pool_.get(nei_chunk_arr_handle), padded_blocks);
    const MesherDataHandle md_handle = mesher_data_pool_.alloc();
    greedy_mesher::MeshData& mesh_data = *mesher_data_pool_.get(md_handle);
    mesh_data.vertices = &gpu_upload_data.vertices;
    mesh_data.resize();

    // TODO: thread safe?
    Chunk* chunk = chunk_pool_.get(handle);
    ASSERT(chunk);
    // TODO: improve
    mesh_data.opaqueMask.assign(mesh_data.opaqueMask.size(), 0);
    mesh_data.forwardMerged.assign(mesh_data.forwardMerged.size(), 0);
    mesh_data.rightMerged.assign(mesh_data.rightMerged.size(), 0);
    mesh_data.faceMasks.assign(mesh_data.faceMasks.size(), 0);
    mesh_data.vertices->clear();
    for (int y = 0, i = 0; y < k_chunk_len_padded; y++) {
      for (int x = 0; x < k_chunk_len_padded; x++) {
        for (int z = 0; z < k_chunk_len_padded; z++, i++) {
          if (padded_blocks[i]) {
            mesh_data.opaqueMask[(y * k_chunk_len_padded) + x] |= 1ull << z;
          }
        }
      }
    }
    greedy_mesher::mesh(padded_blocks.data(), mesh_data);
    LINFO("{} {} {}", chunk->has_terrain, chunk->non_air_block_count, mesh_data.vertexCount);
    mesh_data.vertices = nullptr;
    padded_chunk_voxel_arr_pool_.destroy(padded_chunk_block_handle);
    nei_chunks_arr_pool_.destroy(nei_chunk_arr_handle);
    gpu_upload_data.face_vert_begin = mesh_data.faceVertexBegin;
    gpu_upload_data.face_vert_length = mesh_data.faceVertexLength;
    gpu_upload_data.quad_count = mesh_data.vertexCount;
    chunk->has_mesh = true;

    chunk_gpu_upload_q_.enqueue(std::move(gpu_upload_data));

    mesher_data_pool_.destroy(md_handle);
  });
}
}  // namespace vox
