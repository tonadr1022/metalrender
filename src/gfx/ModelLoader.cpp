#include "ModelLoader.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image/stb_image.h>
#define CGLTF_IMPLEMENTATION
#include <cgltf/cgltf.h>

#include <Metal/Metal.hpp>
#include <format>

#include "RendererMetal.hpp"
#include "metal/MetalUtil.hpp"

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

  for (size_t i = 0; i < gltf->images_count; i++) {
    const cgltf_image &img = gltf->images[i];
    if (!img.buffer_view) {
      int w, h, comp;
      std::filesystem::path full_img_path = directory_path / img.uri;
      uint8_t *data = stbi_load(full_img_path.c_str(), &w, &h, &comp, 4);
      uint32_t mip_levels = std::floor(std::log2(std::max(w, h))) + 1;
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
  }

  auto &materials = result.materials;
  for (size_t material_i = 0; material_i < gltf->materials_count; material_i++) {
    cgltf_material *gltf_mat = &gltf->materials[material_i];
    size_t tex_idx = gltf_mat->pbr_metallic_roughness.base_color_texture.texture - gltf->textures;
    Material material{.albedo = texture_uploads[tex_idx].idx};
    materials.push_back(material);
  }

  auto &vertices = result.vertices;
  auto &indices = result.indices;
  auto &meshes = result.model.meshes;
  for (size_t mesh_i = 0; mesh_i < gltf->meshes_count; mesh_i++) {
    const auto &mesh = gltf->meshes[mesh_i];
    for (size_t prim_i = 0; prim_i < mesh.primitives_count; prim_i++) {
      const auto &primitive = mesh.primitives[prim_i];

      size_t base_vertex = vertices.size();
      size_t vertex_offset = base_vertex * sizeof(DefaultVertex);
      size_t index_offset = SIZE_T_MAX;
      size_t index_count = SIZE_T_MAX;
      if (primitive.indices) {
        index_count = primitive.indices->count;
        // TODO: uint32_t indices
        index_offset = indices.size() * sizeof(uint16_t);
        indices.reserve(indices.size() + index_count);
        for (size_t i = 0; i < primitive.indices->count; i++) {
          indices.push_back(cgltf_accessor_read_index(primitive.indices, i));
        }
      }
      size_t vertex_count{};
      for (size_t attr_i = 0; attr_i < primitive.attributes_count; attr_i++) {
        const auto &attr = primitive.attributes[attr_i];
        cgltf_accessor *accessor = attr.data;
        if (attr.type == cgltf_attribute_type_position) {
          vertex_count = accessor->count;
          vertices.resize(vertices.size() + vertex_count);

          for (size_t i = 0; i < accessor->count; i++) {
            float pos[3] = {0, 0, 0};
            cgltf_accessor_read_float(accessor, i, pos, 3);
            vertices[base_vertex + i].pos = glm::vec4{pos[0], pos[1], pos[2], 0};
          }
        } else if (attr.type == cgltf_attribute_type_texcoord) {
          float uv[2] = {0, 0};
          for (size_t i = 0; i < accessor->count; i++) {
            cgltf_accessor_read_float(accessor, i, uv, 2);
            vertices[base_vertex + i].uv = glm::vec2{uv[0], uv[1]};
          }
        } else if (attr.type == cgltf_attribute_type_normal) {
          float normal[3] = {0, 0, 1};
          for (size_t i = 0; i < accessor->count; i++) {
            cgltf_accessor_read_float(accessor, i, normal, 3);
            vertices[base_vertex + i].normal = glm::vec3{normal[0], normal[1], normal[2]};
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

  return result;
}
