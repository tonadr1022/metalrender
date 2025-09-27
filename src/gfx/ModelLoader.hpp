#pragma once

#include <expected>
#include <filesystem>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <string>

#include "GFXTypes.hpp"
#include "default_vertex.h"
#include "meshoptimizer.h"

namespace MTL {
class Texture;
class Device;
}  // namespace MTL

struct TextureUpload {
  const void *data;
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
  uint32_t vertex_offset;  // element count
  uint32_t index_offset;   // index count
  uint32_t vertex_count;
  uint32_t index_count;
  uint32_t material_id;
};

struct Node {
  glm::mat4 local_transform{1};
  glm::mat4 global_transform{1};
  std::vector<uint32_t> children;
  uint32_t mesh_id{UINT32_MAX};
};

struct MeshletLoadResult {
  std::vector<meshopt_Meshlet> meshlets;
  std::vector<uint32_t> meshlet_vertices;
  std::vector<uint8_t> meshlet_triangles;
  uint32_t meshlet_base{};              // element offset
  uint32_t meshlet_vertices_offset{};   // element offset
  uint32_t meshlet_triangles_offset{};  // element offset
};

struct Model {
  constexpr static uint32_t invalid_id = UINT32_MAX;
  std::vector<Node> nodes;
  uint32_t tot_mesh_nodes{};
  std::vector<uint32_t> root_nodes;
  glm::mat4 root_transform{1};
};

struct MeshletProcessResult {
  size_t tot_meshlet_count{};
  size_t tot_meshlet_verts_count{};
  size_t tot_meshlet_tri_count{};
  std::vector<MeshletLoadResult> meshlet_datas;
};

struct ModelLoadResult {
  Model model;
  std::vector<Mesh> meshes;
  std::vector<DefaultVertex> vertices;
  std::vector<rhi::DefaultIndexT> indices;
  std::vector<TextureUpload> texture_uploads;
  std::vector<Material> materials;
  MeshletProcessResult meshlet_process_result;
};

void update_global_transforms(Model &model);

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
