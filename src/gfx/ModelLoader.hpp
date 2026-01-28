#pragma once

#include <filesystem>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>

#include "ModelInstance.hpp"
#include "RendererTypes.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "hlsl/default_vertex.h"

namespace MTL {
class Texture;
class Device;
}  // namespace MTL

enum class CPUTextureLoadType : uint8_t {
  None,
  StbImage,
  Ktx2,
  Malloc,
};

struct TextureUpload {
  std::unique_ptr<void, UntypedDeleterFuncPtr> data{nullptr, free};
  rhi::TextureDesc desc;
  uint32_t bytes_per_row;
  uint32_t compressed_blocks_tall;
  CPUTextureLoadType load_type{CPUTextureLoadType::None};
};

struct TextureArrayUpload {
  std::vector<void *> data;
  CPUTextureLoadType cpu_type;
  rhi::TextureHandle tex;
  glm::uvec3 dims;
  uint32_t bytes_per_row;
};

struct Material {
  uint32_t albedo_tex{};
  uint32_t normal_tex{};
  glm::vec4 albedo_factors{1};
};

struct Mesh {
  uint32_t vertex_offset_bytes;  // element count
  uint32_t index_offset;         // index count
  uint32_t vertex_count;
  uint32_t index_count;
  uint32_t material_id;
  uint32_t meshlet_count;
  // bounding sphere
  glm::vec3 center;
  float radius;
  constexpr static uint32_t k_invalid_mesh_id = UINT32_MAX;
};

struct MeshletLoadResult {
  std::vector<Meshlet> meshlets;
  std::vector<uint32_t> meshlet_vertices;
  std::vector<uint8_t> meshlet_triangles;
  uint32_t meshlet_base{};              // element offset
  uint32_t meshlet_vertices_offset{};   // element offset
  uint32_t meshlet_triangles_offset{};  // element offset
};

struct MeshletProcessResult {
  size_t tot_meshlet_count{};
  size_t tot_meshlet_verts_count{};
  size_t tot_meshlet_tri_count{};
  std::vector<MeshletLoadResult> meshlet_datas;
};

struct ModelLoadResult {
  std::vector<Mesh> meshes;
  std::vector<DefaultVertex> vertices;
  std::vector<rhi::DefaultIndexT> indices;
  std::vector<TextureUpload> texture_uploads;
  std::vector<Material> materials;
  MeshletProcessResult meshlet_process_result;
};

// TODO: re-evaluate whether renderer is needed. images can be loaded in renderer itself
namespace model {

bool load_model(const std::filesystem::path &path, const glm::mat4 &root_transform,
                ModelInstance &out_model, ModelLoadResult &out_load_result);

}
