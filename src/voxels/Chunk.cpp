#include "Chunk.hpp"

#include <iostream>

#include "core/EAssert.hpp"
#include "core/Logger.hpp"
#include "core/Util.hpp"
#include "voxels/Types.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

namespace {

// constexpr glm::ivec3 face_offsets[] = {
//     glm::ivec3{-1, 0, 0}, glm::ivec3{-1, 0, 0}, glm::ivec3{0, -1, 0},
//     glm::ivec3{0, 1, 0},  glm::ivec3{0, 0, -1}, glm::ivec3{0, 0, 1},
// };
constexpr glm::ivec3 face_offsets[] = {
    glm::ivec3{0, 0, 1},  glm::ivec3{0, 0, -1}, glm::ivec3{1, 0, 0},
    glm::ivec3{-1, 0, 0}, glm::ivec3{0, 1, 0},  glm::ivec3{0, -1, 0},
};

enum Face {
  Face_Front,  // pos z
  Face_Back,   // neg z
  Face_Right,  // pos x
  Face_Left,   // neg x
  Face_Top,    // pos y
  Face_Bot,    // neg y
};

constexpr std::array<std::array<glm::vec3, 4>, 6> vertex_offsets = {{
    {
        glm::vec3{0, 0, 1},  // pos z
        glm::vec3{1, 0, 1},
        glm::vec3{0, 1, 1},
        glm::vec3{1, 1, 1},
    },
    {
        glm::vec3{0, 0, 0},  // neg z
        glm::vec3{0, 1, 0},
        glm::vec3{1, 0, 0},
        glm::vec3{1, 1, 0},
    },
    {
        glm::vec3{1, 0, 0},  // pos x
        glm::vec3{1, 1, 0},
        glm::vec3{1, 0, 1},
        glm::vec3{1, 1, 1},
    },
    {
        glm::vec3{0, 0, 0},  // neg x
        glm::vec3{0, 0, 1},
        glm::vec3{0, 1, 0},
        glm::vec3{0, 1, 1},
    },
    {

        glm::vec3{0, 1, 0},  // pos y
        glm::vec3{0, 1, 1},
        glm::vec3{1, 1, 0},
        glm::vec3{1, 1, 1},
    },
    {
        glm::vec3{0, 0, 0},  // neg y
        glm::vec3{1, 0, 0},
        glm::vec3{0, 0, 1},
        glm::vec3{1, 0, 1},
    },
}};

}  // namespace

void vox::populate_mesh(const PaddedChunkVoxArr& voxels, MeshResult& result) {
  auto& out_vertices = result.vertices;
  glm::ivec3 chunk_pos{};
  uint32_t index_count = 0;
  uint32_t vertex_count = 0;
  for (chunk_pos.y = 0; chunk_pos.y < k_chunk_len; chunk_pos.y++) {
    for (chunk_pos.z = 0; chunk_pos.z < k_chunk_len; chunk_pos.z++) {
      for (chunk_pos.x = 0; chunk_pos.x < k_chunk_len; chunk_pos.x++) {
        if (!voxels[regular_idx_to_padded(chunk_pos)]) {
          continue;
        }

        for (int face = 0; face < 6; face++) {
          const glm::ivec3 nei_pos = face_offsets[face] + chunk_pos;
          if (voxels[regular_idx_to_padded(nei_pos)]) {
            continue;
          }
          glm::vec3 base_pos = glm::vec3{chunk_pos};
          // add block, neighbor is air
          glm::vec3 p_v0 = base_pos + vertex_offsets[face][0];
          glm::vec3 p_v1 = base_pos + vertex_offsets[face][1];
          glm::vec3 p_v2 = base_pos + vertex_offsets[face][2];
          glm::vec3 p_v3 = base_pos + vertex_offsets[face][3];

          out_vertices.emplace_back(VoxelVertex{glm::vec4{p_v0, 0.0}});
          out_vertices.emplace_back(VoxelVertex{glm::vec4{p_v1, 0.0}});
          out_vertices.emplace_back(VoxelVertex{glm::vec4{p_v2, 0.0}});
          out_vertices.emplace_back(VoxelVertex{glm::vec4{p_v3, 0.0}});
          vertex_count += 4;
          index_count += 6;
        }
      }
    }
  }

  result.index_count = index_count;
  result.vertex_count = vertex_count;
}
