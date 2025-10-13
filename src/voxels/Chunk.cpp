#include "Chunk.hpp"

#include "voxels/Types.hpp"

namespace {

constexpr glm::ivec3 face_offsets[] = {
    glm::ivec3{-1, 0, 0}, glm::ivec3{-1, 0, 0}, glm::ivec3{0, -1, 0},
    glm::ivec3{0, 1, 0},  glm::ivec3{0, 0, -1}, glm::ivec3{0, 0, 1},
};

enum Face {
  Face_Front,  // pos z
  Face_Back,   // neg z
  Face_Right,  // pos x
  Face_Left,   // neg x
  Face_Bot,    // neg y
  Face_Top,    // pos y
};

constexpr std::array<std::array<glm::vec3, 4>, 6> vertex_offsets = {{
    {
        glm::vec3{0, 0, 1},
        glm::vec3{1, 0, 1},
        glm::vec3{0, 1, 1},
        glm::vec3{1, 1, 1},
    },
    {
        glm::vec3{0, 0, -1},
        glm::vec3{0, 1, -1},
        glm::vec3{1, 0, -1},
        glm::vec3{1, 1, -1},
    },
    {
        glm::vec3{1, 0, 0},
        glm::vec3{1, 1, 0},
        glm::vec3{1, 0, 1},
        glm::vec3{1, 1, 1},
    },
    {
        glm::vec3{-1, 0, 0},
        glm::vec3{-1, 0, 1},
        glm::vec3{-1, 1, 0},
        glm::vec3{-1, 1, 1},
    },
    {

        glm::vec3{0, 1, 0},
        glm::vec3{1, 1, 0},
        glm::vec3{0, 1, 1},
        glm::vec3{1, 1, 1},
    },
    {
        glm::vec3{0, -1, 0},
        glm::vec3{0, -1, 1},
        glm::vec3{1, -1, 0},
        glm::vec3{1, -1, 1},
    },
}};

}  // namespace

void vox::populate_mesh(const PaddedChunkVoxArr& voxels, std::vector<VoxelVertex>& out_vertices) {
  glm::ivec3 padded{};
  for (padded.y = 0; padded.y < k_chunk_len; padded.y++) {
    for (padded.z = 0; padded.z < k_chunk_len; padded.z++) {
      for (padded.x = 0; padded.x < k_chunk_len; padded.x++) {
        for (int face = 0; face < 6; face++) {
          const glm::ivec3 nei_pos = face_offsets[face] + padded;
          if (voxels[get_idx(nei_pos)] != 0) {
            continue;
          }
          glm::vec3 base_pos = (glm::vec3{padded} - glm::vec3{1});
          // add block, neighbor is air
          glm::vec3 p_v0 = base_pos + vertex_offsets[face][0];
          glm::vec3 p_v1 = base_pos + vertex_offsets[face][1];
          glm::vec3 p_v2 = base_pos + vertex_offsets[face][2];
          glm::vec3 p_v3 = base_pos + vertex_offsets[face][3];

          out_vertices.emplace_back(VoxelVertex{p_v0});
          out_vertices.emplace_back(VoxelVertex{p_v1});
          out_vertices.emplace_back(VoxelVertex{p_v2});
          out_vertices.emplace_back(VoxelVertex{p_v3});
        }
      }
    }
  }
}
