#include "ModelLoader.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image/stb_image.h>
#define CGLTF_IMPLEMENTATION
#include <cgltf/cgltf.h>

#include <Metal/Metal.hpp>
#include <format>

#include "metal/MetalUtil.hpp"

namespace {

// TODO: move
MTL::Texture *load_image(const TextureDesc &desc, MTL::Device *device) {
  MTL::TextureDescriptor *texture_desc = MTL::TextureDescriptor::alloc()->init();
  texture_desc->setWidth(desc.dims.x);
  texture_desc->setHeight(desc.dims.y);
  texture_desc->setDepth(desc.dims.z);
  texture_desc->setPixelFormat(util::mtl::convert_format(desc.format));
  texture_desc->setStorageMode(util::mtl::convert_storage_mode(desc.storage_mode));
  texture_desc->setMipmapLevelCount(desc.mip_levels);
  texture_desc->setArrayLength(desc.array_length);
  texture_desc->setAllowGPUOptimizedContents(true);
  texture_desc->setUsage(MTL::TextureUsageShaderRead);
  MTL::Texture *tex = device->newTexture(texture_desc);
  texture_desc->release();
  return tex;
}
}  // namespace

std::expected<ModelLoadResult, std::string> ResourceManager::load_model(
    const std::filesystem::path &path, MTL::Device *device) {
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
      MTL::Texture *mtl_img = load_image(desc, device);
      texture_uploads.emplace_back(TextureUpload{
          .data = data, .tex = mtl_img, .dims = desc.dims, .bytes_per_row = desc.dims.x * 4});
      if (!data) {
        assert(0);
      }
    } else {
      assert(0 && "need to handle yet");
    }
  }

  auto &vertices = result.vertices;
  auto &indices = result.indices;
  auto &meshes = result.model.meshes;
  for (size_t mesh_i = 0; mesh_i < gltf->meshes_count; mesh_i++) {
    const auto &mesh = gltf->meshes[mesh_i];
    for (size_t prim_i = 0; prim_i < mesh.primitives_count; prim_i++) {
      const auto &primitive = mesh.primitives[prim_i];
      size_t base_vertex = vertices.size();
      size_t vertex_count = gltf->accessors[primitive.attributes[0].index].count;
      vertices.resize(vertices.size() + vertex_count);
      if (primitive.indices) {
        size_t primitive_index_count = primitive.indices->count;
        indices.reserve(indices.size() + primitive_index_count);
        for (size_t i = 0; i < primitive.indices->count; i++) {
          indices.push_back(cgltf_accessor_read_index(primitive.indices, i));
        }
        meshes.push_back(Mesh{
            .vertex_count = vertex_count,
            .index_count = primitive_index_count,
        });
      }
      for (size_t attr_i = 0; attr_i < primitive.attributes_count; attr_i++) {
        if (primitive.material) {
          // const cgltf_material &material = *primitive.material;
          // const cgltf_texture_view &base_color_tex =
          //     material.pbr_metallic_roughness.base_color_texture;
        }
        const auto &attr = primitive.attributes[attr_i];
        cgltf_accessor *accessor = attr.data;
        if (attr.type == cgltf_attribute_type_position) {
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
    }
  }

  return result;
}
