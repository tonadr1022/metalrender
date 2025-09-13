#pragma once

#include <expected>
#include <filesystem>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <string>

#include "GFXTypes.hpp"

namespace MTL {
class Texture;
class Device;
}  // namespace MTL

struct TextureUpload {
  void *data;
  MTL::Texture *tex;
  uint32_t idx;
  glm::uvec3 dims;
  uint32_t bytes_per_row;
};

struct Material {
  uint32_t albedo_tex{UINT32_MAX};
  uint32_t normal_tex{UINT32_MAX};
};

struct Mesh {
  size_t vertex_offset;  // element count
  size_t index_offset;   // index count
  size_t vertex_count;
  size_t index_count;
  size_t material_id;
};

struct Node {
  glm::mat4 local_transform{1};
  glm::mat4 global_transform{1};
  std::vector<uint32_t> children;
  uint32_t mesh_id{UINT32_MAX};
};

struct Model {
  constexpr static uint32_t invalid_id = UINT32_MAX;
  std::vector<Node> nodes;
  std::vector<uint32_t> root_nodes;
  std::vector<Mesh> meshes;
  glm::mat4 root_transform{1};
};

struct ModelLoadResult {
  Model model;
  std::vector<DefaultVertex> vertices;
  std::vector<uint16_t> indices;
  std::vector<TextureUpload> texture_uploads;
  std::vector<Material> materials;
};

void update_global_transforms(const Model &model);

class RendererMetal;

class ResourceManager {
 public:
  static void init() {
    assert(!instance_);
    instance_ = new ResourceManager;
  }
  static void shutdown() {
    assert(instance_);
    delete instance_;
    instance_ = nullptr;
  }
  static ResourceManager &get() {
    assert(instance_);
    return *instance_;
  }

  std::expected<ModelLoadResult, std::string> load_model(const std::filesystem::path &path,
                                                         RendererMetal &renderer);

 private:
  inline static ResourceManager *instance_{};
};
