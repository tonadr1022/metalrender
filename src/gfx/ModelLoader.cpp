#include "ModelLoader.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image/stb_image.h>
#define CGLTF_IMPLEMENTATION
#include <cgltf/cgltf.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <Metal/Metal.hpp>
#include <format>
#include <glm/gtx/quaternion.hpp>
#include <span>

#include "RendererMetal.hpp"
#include "meshoptimizer.h"

namespace {

glm::mat4 calc_transform(const glm::vec3 &translation, const glm::quat &rotation,
                         const glm::vec3 &scale) {
  return glm::translate(glm::mat4{1}, translation) * glm::mat4_cast(rotation) *
         glm::scale(glm::mat4{1}, scale);
}

void update_global_transforms(Model &model, uint32_t node_i, const glm::mat4 &parent_transform) {
  Node &node = model.nodes[node_i];
  node.global_transform = parent_transform * node.local_transform;
  for (auto child : node.children) {
    update_global_transforms(model, child, node.global_transform);
  }
}

// Ref: https://github.com/zeux/meshoptimizer
MeshletData load_meshlet_data(std::span<DefaultVertex> vertices, std::span<IndexT> indices,
                              uint32_t base_vertex) {
  const size_t max_meshlets = meshopt_buildMeshletsBound(indices.size(), k_max_vertices_per_meshlet,
                                                         k_max_triangles_per_meshlet);
  // cone_weight set to a value between 0 and 1 to balance cone culling efficiency with other forms
  // of culling like frustum or occlusion culling (0.25 is a reasonable default).
  const float cone_weight{0.0f};
  std::vector<meshopt_Meshlet> meshlets(max_meshlets);
  std::vector<uint32_t> meshlet_vertices(indices.size());
  std::vector<uint8_t> meshlet_triangles(indices.size());

  const size_t meshlet_count = meshopt_buildMeshlets(
      meshlets.data(), meshlet_vertices.data(), meshlet_triangles.data(), indices.data(),
      indices.size(), &vertices[0].pos.x, vertices.size(), sizeof(DefaultVertex),
      k_max_vertices_per_meshlet, k_max_triangles_per_meshlet, cone_weight);

  const meshopt_Meshlet &last = meshlets[meshlet_count - 1];
  meshlet_vertices.resize(last.vertex_offset + last.vertex_count);
  meshlet_triangles.resize(last.triangle_offset + (last.triangle_count * 3));
  meshlets.resize(meshlet_count);

  for (auto &v : meshlet_vertices) {
    v += base_vertex;
  }
  for (auto &m : meshlets) {
    meshopt_optimizeMeshlet(&meshlet_vertices[m.vertex_offset],
                            &meshlet_triangles[m.triangle_offset], m.triangle_count,
                            m.vertex_count);
  }

  return MeshletData{.meshlets = std::move(meshlets),
                     .meshlet_vertices = std::move(meshlet_vertices),
                     .meshlet_triangles = std::move(meshlet_triangles)};
}

}  // namespace

void update_global_transforms(Model &model) {
  for (const uint32_t root_node_i : model.root_nodes) {
    update_global_transforms(model, root_node_i, model.root_transform);
  }
}

std::expected<ModelLoadResult, std::string> ResourceManager::load_model(
    const std::filesystem::path &path, RendererMetal &renderer) {
  const cgltf_options gltf_load_opts{};
  cgltf_data *raw_gltf{};
  cgltf_result gltf_res = cgltf_parse_file(&gltf_load_opts, path.c_str(), &raw_gltf);
  std::unique_ptr<cgltf_data, void (*)(cgltf_data *)> gltf(raw_gltf, cgltf_free);
  std::filesystem::path directory_path = path.parent_path();

  if (gltf_res != cgltf_result_success) {
    if (gltf_res == cgltf_result_file_not_found) {
      return std::unexpected(std::format("Failed to load GLTF. File not found {}", path.c_str()));
    }
    return std::unexpected(std::format("Failed to laod GLTF with error {} for file {}",
                                       static_cast<int>(gltf_res), path.c_str()));
  }

  gltf_res = cgltf_load_buffers(&gltf_load_opts, gltf.get(), path.c_str());

  if (gltf_res != cgltf_result_success) {
    return std::unexpected(
        std::format("Failed to load GLTF buffers for gltf path {}", path.c_str()));
  }

  ModelLoadResult result;

  auto &texture_uploads = result.texture_uploads;
  texture_uploads.reserve(gltf->images_count);

  auto load_img = [&](uint32_t gltf_img_i) -> uint32_t {
    const cgltf_image &img = gltf->images[gltf_img_i];
    if (!img.buffer_view) {
      int w, h, comp;
      const std::filesystem::path full_img_path = directory_path / img.uri;
      const uint8_t *data = stbi_load(full_img_path.c_str(), &w, &h, &comp, 4);
      const uint32_t mip_levels = static_cast<uint32_t>(std::floor(std::log2(std::max(w, h)))) + 1;
      const TextureDesc desc{.format = TextureFormat::R8G8B8A8Unorm,
                             .storage_mode = StorageMode::GPUOnly,
                             .dims = glm::uvec3{w, h, 1},
                             .mip_levels = mip_levels,
                             .array_length = 1};
      const TextureWithIdx texture_with_idx = renderer.load_material_image(desc);
      texture_uploads.emplace_back(TextureUpload{.data = data,
                                                 .tex = texture_with_idx.tex,
                                                 .idx = texture_with_idx.idx,
                                                 .dims = desc.dims,
                                                 .bytes_per_row = desc.dims.x * 4});
      if (!data) {
        assert(0);
      }
      return texture_with_idx.idx;
    }
    assert(0 && "need to handle yet");
    return UINT32_MAX;
  };

  auto &result_nodes = result.model.nodes;

  auto &materials = result.materials;
  for (size_t material_i = 0; material_i < gltf->materials_count; material_i++) {
    const cgltf_material *gltf_mat = &gltf->materials[material_i];
    Material material{};
    auto set_and_load_material_img = [&gltf, &load_img](const cgltf_texture_view *tex_view,
                                                        uint32_t &result_tex_id) {
      if (!tex_view || !tex_view->texture || !tex_view->texture->image) {
        return;
      }
      const size_t gltf_image_i = tex_view->texture->image - gltf->images;
      result_tex_id = load_img(gltf_image_i);
    };

    set_and_load_material_img(&gltf_mat->pbr_metallic_roughness.base_color_texture,
                              material.albedo_tex);
    set_and_load_material_img(&gltf_mat->normal_texture, material.normal_tex);

    materials.push_back(material);
  }

  auto &all_vertices = result.model.vertices;
  auto &all_indices = result.model.indices;
  auto &meshes = result.model.meshes;
  std::vector<uint32_t> gltf_node_to_node_i;
  gltf_node_to_node_i.resize(gltf->nodes_count, UINT32_MAX);

  std::vector<std::vector<uint32_t>> primitive_mesh_indices_per_mesh;
  primitive_mesh_indices_per_mesh.resize(gltf->meshes_count);

  {  // process mesh/primitive
    uint32_t overall_mesh_i = 0;
    for (uint32_t mesh_i = 0; mesh_i < gltf->meshes_count; mesh_i++) {
      const auto &mesh = gltf->meshes[mesh_i];
      for (uint32_t prim_i = 0; prim_i < mesh.primitives_count; prim_i++, overall_mesh_i++) {
        primitive_mesh_indices_per_mesh[mesh_i].push_back(overall_mesh_i);
        const cgltf_primitive &primitive = mesh.primitives[prim_i];

        const auto base_vertex = static_cast<uint32_t>(all_vertices.size());
        const size_t vertex_offset = base_vertex * sizeof(DefaultVertex);
        uint32_t index_offset = UINT32_MAX;
        uint32_t index_count = UINT32_MAX;
        if (primitive.indices) {
          index_count = primitive.indices->count;
          // TODO: uint32_t indices
          index_offset = all_indices.size() * sizeof(IndexT);
          all_indices.reserve(all_indices.size() + index_count);
          for (size_t i = 0; i < primitive.indices->count; i++) {
            all_indices.push_back(cgltf_accessor_read_index(primitive.indices, i));
          }
        }
        assert(primitive.attributes_count > 0);
        const auto vertex_count = static_cast<uint32_t>(primitive.attributes[0].data->count);
        all_vertices.resize(all_vertices.size() + vertex_count);
        for (size_t attr_i = 0; attr_i < primitive.attributes_count; attr_i++) {
          const auto &attr = primitive.attributes[attr_i];
          const cgltf_accessor *accessor = attr.data;
          if (attr.type == cgltf_attribute_type_position) {
            for (size_t i = 0; i < accessor->count; i++) {
              float pos[3] = {0, 0, 0};
              cgltf_accessor_read_float(accessor, i, pos, 3);
              all_vertices[base_vertex + i].pos = glm::vec4{pos[0], pos[1], pos[2], 0};
            }
          } else if (attr.type == cgltf_attribute_type_texcoord) {
            for (size_t i = 0; i < accessor->count; i++) {
              float uv[2] = {0, 0};
              cgltf_accessor_read_float(accessor, i, uv, 2);
              all_vertices[base_vertex + i].uv = glm::vec2{uv[0], uv[1]};
            }
          } else if (attr.type == cgltf_attribute_type_normal) {
            float normal[3] = {0, 0, 0};
            for (size_t i = 0; i < accessor->count; i++) {
              cgltf_accessor_read_float(accessor, i, normal, 3);
              all_vertices[base_vertex + i].normal = glm::vec3{normal[0], normal[1], normal[2]};
            }
          }
        }

        const uint32_t material_idx =
            primitive.material ? primitive.material - gltf->materials : UINT32_MAX;
        assert(vertex_offset < UINT32_MAX);
        meshes.push_back(Mesh{
            .vertex_offset = static_cast<uint32_t>(vertex_offset),
            .index_offset = index_offset,
            .vertex_count = vertex_count,
            .index_count = index_count,
            .material_id = material_idx,
        });
      }
    }
    auto &meshlet_datas = result.model.meshlet_datas;
    for (const Mesh &mesh : meshes) {
      uint32_t base_vertex = mesh.vertex_offset / sizeof(DefaultVertex);
      meshlet_datas.emplace_back(load_meshlet_data(
          std::span(&all_vertices[base_vertex], mesh.vertex_count),
          std::span(&all_indices[mesh.index_offset / sizeof(IndexT)], mesh.index_count),
          base_vertex));
    }
  }
  {
    // process nodes
    uint32_t tot_mesh_nodes = 0;
    for (size_t node_i = 0; node_i < gltf->nodes_count; node_i++) {
      const cgltf_node &gltf_node = gltf->nodes[node_i];

      const glm::vec3 translation =
          gltf_node.has_translation ? glm::vec3{gltf_node.translation[0], gltf_node.translation[1],
                                                gltf_node.translation[2]}
                                    : glm::vec3{};
      const glm::quat rotation = gltf_node.has_rotation
                                     ? glm::quat{gltf_node.rotation[3], gltf_node.rotation[0],
                                                 gltf_node.rotation[1], gltf_node.rotation[2]}
                                     : glm::identity<glm::quat>();
      const glm::vec3 scale =
          gltf_node.has_scale
              ? glm::vec3{gltf_node.scale[0], gltf_node.scale[1], gltf_node.scale[2]}
              : glm::vec3{1};

      std::vector<uint32_t> children;
      if (gltf_node.mesh) {
        auto mesh_id = static_cast<uint32_t>(gltf_node.mesh - gltf->meshes);
        children.reserve(primitive_mesh_indices_per_mesh[mesh_id].size());
        for (auto prim_mesh_id : primitive_mesh_indices_per_mesh[mesh_id]) {
          children.push_back(static_cast<uint32_t>(result_nodes.size()));
          tot_mesh_nodes++;
          result_nodes.emplace_back(Node{.mesh_id = prim_mesh_id});
        }
      }

      children.reserve(children.size() + gltf_node.children_count);
      for (uint32_t ci = 0; ci < gltf_node.children_count; ci++) {
        children.push_back(
            gltf_node_to_node_i[static_cast<uint32_t>(gltf_node.children[ci] - gltf->nodes)]);
      }

      gltf_node_to_node_i[node_i] = static_cast<uint32_t>(result_nodes.size());
      result_nodes.emplace_back(
          Node{.local_transform = calc_transform(translation, rotation, scale),
               .children = std::move(children)});
    }

    assert(gltf->scenes_count == 1);
    const auto *scene = &gltf->scenes[0];
    auto &root_nodes = result.model.root_nodes;
    root_nodes.reserve(scene->nodes_count);
    for (uint32_t i = 0; i < scene->nodes_count; i++) {
      root_nodes.push_back(gltf_node_to_node_i[scene->nodes[i] - gltf->nodes]);
    }

    result.model.tot_mesh_nodes = tot_mesh_nodes;
    update_global_transforms(result.model);
  }

  return result;
}
