#include "Chunk.hpp"

#include <tracy/Tracy.hpp>

#include "voxels/Types.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

namespace {

enum Face {
  Face_Right,  // pos x
  Face_Left,   // neg x
  Face_Top,    // pos y
  Face_Bot,    // neg y
  Face_Front,  // pos z
  Face_Back,   // neg z
};

constexpr std::array<std::array<glm::ivec3, 4>, 6> vertex_offsets = {{
    {
        glm::ivec3{1, 0, 0},  // pos x
        glm::ivec3{1, 1, 0},
        glm::ivec3{1, 0, 1},
        glm::ivec3{1, 1, 1},
    },
    {
        glm::ivec3{0, 0, 0},  // neg x
        glm::ivec3{0, 0, 1},
        glm::ivec3{0, 1, 0},
        glm::ivec3{0, 1, 1},
    },
    {

        glm::ivec3{0, 1, 0},  // pos y
        glm::ivec3{0, 1, 1},
        glm::ivec3{1, 1, 0},
        glm::ivec3{1, 1, 1},
    },
    {
        glm::ivec3{0, 0, 0},  // neg y
        glm::ivec3{1, 0, 0},
        glm::ivec3{0, 0, 1},
        glm::ivec3{1, 0, 1},
    },
    {
        glm::ivec3{0, 0, 1},  // pos z
        glm::ivec3{1, 0, 1},
        glm::ivec3{0, 1, 1},
        glm::ivec3{1, 1, 1},
    },
    {
        glm::ivec3{0, 0, 0},  // neg z
        glm::ivec3{0, 1, 0},
        glm::ivec3{1, 0, 0},
        glm::ivec3{1, 1, 0},
    },
}};

constexpr uint32_t encode_vertex(int x, int y, int z, uint8_t material, uint8_t ao) {
  return (x) | (y << 7) | (z << 14) | (material << 21) | (ao << 30);
}

constexpr uint8_t encode_color_216(uint8_t r_scaled, uint8_t g_scaled, uint8_t b_scaled) {
  return 16 + (36 * r_scaled) + (6 * g_scaled) + b_scaled;
}

}  // namespace

namespace vox {

namespace {

[[maybe_unused]] uint8_t get_ao(int face, int v, const vox::VoxelId block_neighbors[27]) {
  // y
  // |
  // |  6   15  24
  // |    7   16  25
  // |      8   17  26
  // |
  // |  3   12  21
  // |    4   13  22
  // |      5   14  23
  // \-------------------x
  //  \ 0   9   18
  //   \  1   10  19
  //    \   2   11  20
  //     z
  constexpr const int k_ao_lookups[6][4][3] = {
      {{21, 18, 19}, {21, 24, 25}, {23, 20, 19}, {23, 26, 25}},  // pos x
      {{3, 0, 1}, {5, 2, 1}, {3, 6, 7}, {5, 8, 7}},              // neg x
      {{15, 6, 7}, {17, 8, 7}, {15, 24, 25}, {17, 26, 25}},      // pos y
      {{9, 0, 1}, {9, 18, 19}, {11, 2, 1}, {11, 20, 19}},        // neg y
      {{11, 2, 5}, {11, 20, 23}, {17, 8, 5}, {17, 26, 23}},      // pos z
      {{9, 0, 3}, {15, 6, 3}, {9, 18, 21}, {15, 24, 21}},        // neg z
  };
  VoxelId n0 = block_neighbors[k_ao_lookups[face][v][0]];
  VoxelId n1 = block_neighbors[k_ao_lookups[face][v][1]];
  VoxelId n2 = block_neighbors[k_ao_lookups[face][v][2]];
  bool trans[3] = {!n0, !n1, !n2};
  return (!trans[0] && !trans[2] ? 0 : 3 - !trans[0] - !trans[1] - !trans[2]);
}

}  // namespace

#define BAKED_AO_ENABLED

#define GREEDY_MESHING_ENABLED

void populate_mesh(const PaddedChunkVoxArr& voxels, ChunkUploadData& result) {
  ZoneScoped;
  auto& out_vertices = result.vertices;
  uint32_t vertex_count = 0;
  VoxelId block_neighbors[27];
  for (int y = 0; y < k_chunk_len; y++) {
    for (int x = 0; x < k_chunk_len; x++) {
      for (int z = 0; z < k_chunk_len; z++) {
        VoxelId vox = voxels[regular_idx_to_padded(x, y, z)];
        bool block_neighbors_initialized{};

        if (!vox) {
          continue;
        }
        uint8_t r{}, g{}, b{};
        if (vox == 1) {
          g = 3;
        }
        if (vox == 2) {
          r = 3;
        }
        if (vox == 3) {
          b = 3;
        }

        uint8_t material = encode_color_216(r, g, b);

        for (int face = 0; face < 6; face++) {
          glm::ivec3 nei_pos{x, y, z};
          nei_pos[face >> 1] += 1 - ((face & 1) << 1);

          if (voxels[regular_idx_to_padded(nei_pos)]) {
            continue;
          }
          if (!block_neighbors_initialized) {
            block_neighbors_initialized = true;

            for (int nx = -1, i = 0; nx <= 1; nx++) {
              for (int ny = -1; ny <= 1; ny++) {
                for (int nz = -1; nz <= 1; nz++, i++) {
                  block_neighbors[i] = voxels[regular_idx_to_padded(x + nx, y + ny, z + nz)];
                }
              }
            }
          }

          for (int v = 0; v < 4; v++) {
            int pos[3] = {x + vertex_offsets[face][v][0], y + vertex_offsets[face][v][1],
                          z + vertex_offsets[face][v][2]};
            uint8_t ao = 0;
#ifdef BAKED_AO_ENABLED
            ao = get_ao(face, v, block_neighbors);
#endif
            out_vertices.emplace_back(encode_vertex(pos[0], pos[1], pos[2], material, ao));
          }

          vertex_count += 4;
        }
      }
    }
  }

  result.quad_count = vertex_count;
}

}  // namespace vox
