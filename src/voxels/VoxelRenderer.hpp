#pragma once

#include <glm/vec3.hpp>
#include <span>

namespace vox {

struct VoxelVertex {
  glm::vec3 pos;
};

class Renderer {
public:
  void upload_chunk(std::span<VoxelVertex> vertices, std::span<uint32_t> indices);
private:
};

}
