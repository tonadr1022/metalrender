#include "ModelLoader.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image/stb_image.h>
#define CGLTF_IMPLEMENTATION
#include <cgltf/cgltf.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <Metal/Metal.hpp>
#include <format>
#include <glm/gtx/quaternion.hpp>

#include "RendererMetal.hpp"
#include "metal/MetalUtil.hpp"

namespace {

glm::mat4 calc_transform(const glm::vec3 &translation, const glm::quat &rotation,
                         const glm::vec3 &scale) {
  glm::mat4 S = glm::scale(glm::mat4{1}, scale);
  glm::mat4 R = glm::mat4_cast(rotation);
  return glm::translate(glm::mat4{1}, translation) * R * S;
}

void update_global_transforms(Model &model, uint32_t node_i, const glm::mat4 &parent_transform) {
  Node &node = model.nodes[node_i];
  node.global_transform = parent_transform * node.local_transform;
  for (auto child : node.children) {
    update_global_transforms(model, child, node.global_transform);
  }
}

}  // namespace

void update_global_transforms(Model &model) {
  for (uint32_t root_node_i : model.root_nodes) {
    update_global_transforms(model, root_node_i, model.root_transform);
  }
}

std::expected<ModelLoadResult, std::string> ResourceManager::load_model(
    const std::filesystem::path &path, RendererMetal &renderer) {
  cgltf_options gltf_load_opts{};
  cgltf_data *raw_gltf{};
  cgltf_result gltf_res = cgltf_parse_file(&gltf_load_opts, path.c_str(), &raw_gltf);
  std::unique_ptr<cgltf_data, void (*)(cgltf_data *)> gltf(raw_gltf, cgltf_free);
  std::filesystem::path directory_path = path.parent_path();

  if (gltf_res != cgltf_result_success) {
    if (gltf_res == cgltf_result_file_not_found) {
      return std::unexpected(std::format("Failed to load GLTF. File not found {}", path.c_str()));
    } else {
      return std::unexpected(std::format("Failed to laod GLTF with error {} for file {}",
                                         static_cast<int>(gltf_res), path.c_str()));
    }
  }

  gltf_res = cgltf_load_buffers(&gltf_load_opts, gltf.get(), path.c_str());

  if (gltf_res != cgltf_result_success) {
    return std::unexpected(
        std::format("Failed to load GLTF buffers for gltf path {}", path.c_str()));
  }

  ModelLoadResult result;

  auto &texture_uploads = result.texture_uploads;
  texture_uploads.reserve(gltf->images_count);

  auto load_img = [&](uint32_t gltf_img_i) {
    const cgltf_image &img = gltf->images[gltf_img_i];
    if (!img.buffer_view) {
      int w, h, comp;
      std::filesystem::path full_img_path = directory_path / img.uri;
      uint8_t *data = stbi_load(full_img_path.c_str(), &w, &h, &comp, 4);
      uint32_t mip_levels = static_cast<uint32_t>(std::floor(std::log2(std::max(w, h)))) + 1;
      TextureDesc desc{.format = TextureFormat::R8G8B8A8Unorm,
                       .storage_mode = StorageMode::GPUOnly,
                       .dims = glm::uvec3{w, h, 1},
                       .mip_levels = mip_levels,
                       .array_length = 1,
                       .data = data};
      TextureWithIdx texture_with_idx = renderer.load_material_image(desc);
      texture_uploads.emplace_back(TextureUpload{.data = data,
                                                 .tex = texture_with_idx.tex,
                                                 .idx = texture_with_idx.idx,
                                                 .dims = desc.dims,
                                                 .bytes_per_row = desc.dims.x * 4});
      if (!data) {
        assert(0);
      }
    } else {
      assert(0 && "need to handle yet");
    }
  };

  auto &result_nodes = result.model.nodes;

  auto &materials = result.materials;
  for (size_t material_i = 0; material_i < gltf->materials_count; material_i++) {
    cgltf_material *gltf_mat = &gltf->materials[material_i];
    size_t gltf_image_i =
        gltf_mat->pbr_metallic_roughness.base_color_texture.texture->image - gltf->images;
    load_img(gltf_image_i);
    Material material{.albedo = texture_uploads.back().idx};
    materials.push_back(material);
  }

  auto &all_vertices = result.vertices;
  auto &all_indices = result.indices;
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

        size_t base_vertex = all_vertices.size();
        size_t vertex_offset = base_vertex * sizeof(DefaultVertex);
        size_t index_offset = SIZE_T_MAX;
        size_t index_count = SIZE_T_MAX;
        if (primitive.indices) {
          index_count = primitive.indices->count;
          // TODO: uint32_t indices
          index_offset = all_indices.size() * sizeof(uint16_t);
          all_indices.reserve(all_indices.size() + index_count);
          for (size_t i = 0; i < primitive.indices->count; i++) {
            all_indices.push_back(cgltf_accessor_read_index(primitive.indices, i));
          }
        }
        size_t vertex_count{};
        vertex_count = primitive.attributes[0].data->count;
        all_vertices.resize(all_vertices.size() + vertex_count);
        for (size_t attr_i = 0; attr_i < primitive.attributes_count; attr_i++) {
          const auto &attr = primitive.attributes[attr_i];
          cgltf_accessor *accessor = attr.data;
          if (attr.type == cgltf_attribute_type_position) {
            for (size_t i = 0; i < accessor->count; i++) {
              float pos[3] = {0, 0, 0};
              cgltf_accessor_read_float(accessor, i, pos, 3);
              all_vertices[base_vertex + i].pos = glm::vec4{pos[0], pos[1], pos[2], 0};
            }
          } else if (attr.type == cgltf_attribute_type_texcoord) {
            float uv[2] = {0, 0};
            for (size_t i = 0; i < accessor->count; i++) {
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

        size_t material_idx = primitive.material - gltf->materials;
        meshes.push_back(Mesh{
            .vertex_offset = vertex_offset,
            .index_offset = index_offset,
            .vertex_count = vertex_count,
            .index_count = index_count,
            .material_id = material_idx,
        });
      }
    }
  }
  {
    // process nodes
    for (size_t node_i = 0; node_i < gltf->nodes_count; node_i++) {
      const cgltf_node &gltf_node = gltf->nodes[node_i];

      glm::vec3 translation = gltf_node.has_translation
                                  ? glm::vec3{gltf_node.translation[0], gltf_node.translation[1],
                                              gltf_node.translation[2]}
                                  : glm::vec3{};
      glm::quat rotation = gltf_node.has_rotation
                               ? glm::quat{gltf_node.rotation[3], gltf_node.rotation[0],
                                           gltf_node.rotation[1], gltf_node.rotation[2]}
                               : glm::identity<glm::quat>();
      glm::vec3 scale = gltf_node.has_scale
                            ? glm::vec3{gltf_node.scale[0], gltf_node.scale[1], gltf_node.scale[2]}
                            : glm::vec3{1};

      std::vector<uint32_t> children;
      if (gltf_node.mesh) {
        uint32_t mesh_id = static_cast<uint32_t>(gltf_node.mesh - gltf->meshes);
        for (auto prim_mesh_id : primitive_mesh_indices_per_mesh[mesh_id]) {
          children.push_back(static_cast<uint32_t>(result_nodes.size()));
          result_nodes.emplace_back(Node{.mesh_id = prim_mesh_id});
        }
      }

      children.reserve(children.size() + gltf_node.children_count);
      for (uint32_t ci = 0; ci < gltf_node.children_count; ci++) {
        children.push_back(static_cast<uint32_t>(gltf_node.children[ci] - gltf->nodes));
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

    update_global_transforms(result.model);
  }

  return result;
}
