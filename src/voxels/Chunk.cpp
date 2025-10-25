#include "Chunk.hpp"

#include <span>
#include <tracy/Tracy.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

namespace vox {

namespace {

VoxelId get_most_common(std::span<VoxelId> ids) {
  VoxelId mc = ids[0];
  size_t best_count = 1;
  size_t curr_count = 1;
  for (size_t i = 1; i < ids.size(); i++) {
    if (ids[i] == ids[i - 1]) {
      curr_count++;
    } else {
      curr_count = 1;
    }

    if (curr_count > best_count) {
      best_count = curr_count;
      mc = ids[i];
    }
  }

  return mc;
}

}  // namespace

void ChunkBlockArr::fill_lods() {
  // TODO: remove
  lod_blocks.fill(0);
  for (int lod = 1; lod < k_chunk_bits + 1; lod++) {
    VoxelId* curr_voxels = lod == 1 ? blocks.data() : lod_blocks.data();
    auto size = lod == 1 ? blocks.size() : lod_blocks.size();
    int cl = k_chunk_len >> lod;
    for (int y = 0; y < cl; y++) {
      for (int x = 0; x < cl; x++) {
        for (int z = 0; z < cl; z++) {
          VoxelId types[8];
          for (int i = 0; i < 8; i++) {
            // TODO: this is insanity
            if (lod == 1) {
              size_t idx =
                  get_idx((x * 2) + (i & 1), (y * 2) + ((i >> 1) & 1), (z * 2) + ((i >> 2) & 1));
              ASSERT(idx < size);
              types[i] = curr_voxels[idx];
            } else {
              size_t idx = get_idx_lod((x << 1) + (i & 1), (y << 1) + ((i >> 1) & 1),
                                       (z << 1) + ((i >> 2) & 1), lod - 1);
              ASSERT(idx < size);
              types[i] = curr_voxels[idx];
            }
          }
          std::ranges::sort(types);
          VoxelId most_common = get_most_common(types);
          lod_blocks[get_idx_lod(x, y, z, lod)] = most_common;
        }
      }
    }
  }
}
}  // namespace vox
