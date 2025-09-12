#pragma once

#include <expected>
#include <filesystem>
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
  uint32_t albedo{};
};

struct Mesh {
  size_t vertex_count;
  size_t index_count;
  size_t material_id;
};

struct Model {
  std::vector<Mesh> meshes;
};

struct ModelLoadResult {
  Model model;
  std::vector<DefaultVertex> vertices;
  std::vector<uint16_t> indices;
  std::vector<TextureUpload> texture_uploads;
  std::vector<Material> materials;
};

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
  static ResourceManager &get() { return *instance_; }

  std::expected<ModelLoadResult, std::string> load_model(const std::filesystem::path &path,
                                                         RendererMetal &renderer);

 private:
  inline static ResourceManager *instance_{};
};
