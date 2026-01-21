#include "ModelLoader.hpp"

#include <ktx.h>

#include <fstream>
#include <future>
#include <tracy/Tracy.hpp>

#include "core/EAssert.hpp"
#include "core/Logger.hpp"
#include "core/ThreadPool.hpp"
#include "core/Util.hpp"
#include "gfx/GFXTypes.hpp"
#include "hlsl/shader_constants.h"
#include "texture/KtxLoad.hpp"
#include "texture/VkFormatEnum.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image/stb_image.h>
#define CGLTF_IMPLEMENTATION
#include <cgltf/cgltf.h>

#include <Metal/Metal.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <span>
#include <stack>

#include "core/MathUtil.hpp"
#include "meshoptimizer.h"

namespace {

void write_meshlet_data(std::ostream &o_file, const MeshletLoadResult &meshlet_data) {
  ZoneScoped;

  const auto meshlet_count = meshlet_data.meshlets.size();
  o_file.write(reinterpret_cast<const char *>(&meshlet_count), sizeof(uint32_t));
  o_file.write(reinterpret_cast<const char *>(meshlet_data.meshlets.data()),
               sizeof(Meshlet) * meshlet_count);

  const auto meshlet_vertices_count = meshlet_data.meshlet_vertices.size();
  o_file.write(reinterpret_cast<const char *>(&meshlet_vertices_count), sizeof(uint32_t));
  o_file.write(reinterpret_cast<const char *>(meshlet_data.meshlet_vertices.data()),
               sizeof(uint32_t) * meshlet_vertices_count);

  const auto meshlet_triangles_count = meshlet_data.meshlet_triangles.size();
  o_file.write(reinterpret_cast<const char *>(&meshlet_triangles_count), sizeof(uint32_t));
  o_file.write(reinterpret_cast<const char *>(meshlet_data.meshlet_triangles.data()),
               sizeof(uint8_t) * meshlet_triangles_count);
}

void read_meshlet_data(std::istream &i_file, MeshletLoadResult &meshlet_data) {
  ZoneScoped;

  uint32_t meshlet_count{};
  i_file.read(reinterpret_cast<char *>(&meshlet_count), sizeof(uint32_t));
  meshlet_data.meshlets.resize(meshlet_count);
  i_file.read(reinterpret_cast<char *>(meshlet_data.meshlets.data()),
              sizeof(Meshlet) * meshlet_count);

  uint32_t meshlet_vertices_count{};
  i_file.read(reinterpret_cast<char *>(&meshlet_vertices_count), sizeof(uint32_t));
  meshlet_data.meshlet_vertices.resize(meshlet_vertices_count);
  i_file.read(reinterpret_cast<char *>(meshlet_data.meshlet_vertices.data()),
              sizeof(uint32_t) * meshlet_vertices_count);

  uint32_t meshlet_triangles_count{};
  i_file.read(reinterpret_cast<char *>(&meshlet_triangles_count), sizeof(uint32_t));
  meshlet_data.meshlet_triangles.resize(meshlet_triangles_count);
  i_file.read(reinterpret_cast<char *>(meshlet_data.meshlet_triangles.data()),
              sizeof(uint8_t) * meshlet_triangles_count);
}

void write_meshlets(std::ostream &o_file, std::span<const MeshletLoadResult> meshlet_data) {
  ZoneScoped;
  const auto meshlet_data_count = static_cast<uint32_t>(meshlet_data.size());
  o_file.write(reinterpret_cast<const char *>(&meshlet_data_count), sizeof(uint32_t));
  for (const auto &m : meshlet_data) {
    write_meshlet_data(o_file, m);
  }
}

void read_meshlets(std::istream &i_file, std::vector<MeshletLoadResult> &meshlet_data) {
  ZoneScoped;
  uint32_t meshlet_data_count{};
  i_file.read(reinterpret_cast<char *>(&meshlet_data_count), sizeof(uint32_t));
  meshlet_data.resize(meshlet_data_count);
  for (auto &m : meshlet_data) {
    read_meshlet_data(i_file, m);
  }
}

int32_t add_node_to_hierarchy(std::vector<Hierarchy> &hierarchies, int32_t parent, int32_t level) {
  ZoneScoped;
  const auto node_i = static_cast<int32_t>(hierarchies.size());
  hierarchies.push_back(Hierarchy{.parent = parent, .level = level});

  // upate parent
  if (parent != Hierarchy::k_invalid_node_id) {
    auto first_child_of_parent = hierarchies[parent].first_child;
    if (first_child_of_parent == Hierarchy::k_invalid_node_id) {
      // new node is the first child of the parent
      hierarchies[parent].first_child = node_i;
      // node is it's own last sibling
      hierarchies[node_i].last_sibling = node_i;
    } else {
      auto last_sibling = hierarchies[first_child_of_parent].last_sibling;
      if (last_sibling == Hierarchy::k_invalid_node_id) {
        for (last_sibling = first_child_of_parent;
             hierarchies[last_sibling].next_sibling != Hierarchy::k_invalid_node_id;
             last_sibling = hierarchies[last_sibling].next_sibling);
      }
      hierarchies[last_sibling].next_sibling = node_i;
      hierarchies[first_child_of_parent].last_sibling = node_i;
    }
  }
  hierarchies[node_i].next_sibling = -1;
  hierarchies[node_i].first_child = -1;
  hierarchies[node_i].last_sibling = -1;
  return node_i;
}

int32_t add_node_to_model(ModelInstance &model, int32_t parent, int32_t level,
                          const glm::vec3 &translation, const glm::quat &rotation, float scale,
                          uint32_t mesh_id) {
  ZoneScoped;
  const int32_t node = add_node_to_hierarchy(model.nodes, parent, level);
  assert(std::cmp_equal(node, model.global_transforms.size()));
  assert(std::cmp_equal(node, model.local_transforms.size()));
  assert(std::cmp_equal(node, model.mesh_ids.size()));
  if (level >= static_cast<int32_t>(model.changed_this_frame.size())) {
    model.changed_this_frame.resize(level + 1);
  }
  model.global_transforms.emplace_back();
  model.local_transforms.emplace_back(translation, rotation, scale);
  model.mesh_ids.emplace_back(mesh_id);
  return node;
}

// Ref: https://github.com/zeux/meshoptimizer
MeshletLoadResult load_meshlet_data(std::span<DefaultVertex> vertices,
                                    std::span<rhi::DefaultIndexT> indices, uint32_t base_vertex) {
  ZoneScoped;
  const size_t max_meshlets = meshopt_buildMeshletsBound(indices.size(), k_max_vertices_per_meshlet,
                                                         k_max_triangles_per_meshlet);
  // cone_weight set to a value between 0 and 1 to balance cone culling efficiency with other forms
  // of culling like frustum or occlusion culling (0.25 is a reasonable default).
  const float cone_weight{0.25f};
  std::vector<meshopt_Meshlet> meshopt_meshlets(max_meshlets);
  std::vector<uint32_t> meshlet_vertices(max_meshlets * k_max_vertices_per_meshlet);
  std::vector<uint8_t> meshlet_triangles(max_meshlets * k_max_triangles_per_meshlet);

  const size_t meshlet_count = meshopt_buildMeshlets(
      meshopt_meshlets.data(), meshlet_vertices.data(), meshlet_triangles.data(), indices.data(),
      indices.size(), &vertices[0].pos.x, vertices.size(), sizeof(DefaultVertex),
      k_max_vertices_per_meshlet, k_max_triangles_per_meshlet, cone_weight);

  const meshopt_Meshlet &last = meshopt_meshlets[meshlet_count - 1];
  meshlet_vertices.resize(last.vertex_offset + last.vertex_count);
  meshlet_triangles.resize(last.triangle_offset + (last.triangle_count * 3));
  meshopt_meshlets.resize(meshlet_count);

  // TODO: evaluate if this is the problem
  std::vector<Meshlet> meshlets;
  meshlets.resize(meshopt_meshlets.size());
  for (size_t i = 0; i < meshopt_meshlets.size(); i++) {
    const auto &m = meshopt_meshlets[i];
    meshopt_optimizeMeshlet(&meshlet_vertices[m.vertex_offset],
                            &meshlet_triangles[m.triangle_offset], m.triangle_count,
                            m.vertex_count);
    const meshopt_Bounds bounds = meshopt_computeMeshletBounds(
        &meshlet_vertices[m.vertex_offset], &meshlet_triangles[m.triangle_offset], m.triangle_count,
        &vertices[0].pos.x, vertices.size(), sizeof(DefaultVertex));
    Meshlet &meshlet = meshlets[i];
    meshlet.vertex_offset = m.vertex_offset;
    meshlet.triangle_offset = m.triangle_offset;
    meshlet.vertex_count = m.vertex_count;
    meshlet.triangle_count = m.triangle_count;
    meshlet.center_radius = {bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius};
    meshlet.cone_axis_cutoff = (uint32_t(uint8_t(bounds.cone_axis_s8[0]))) |
                               (uint32_t(uint8_t(bounds.cone_axis_s8[1])) << 8) |
                               (uint32_t(uint8_t(bounds.cone_axis_s8[2])) << 16) |
                               (uint32_t(uint8_t(bounds.cone_cutoff_s8)) << 24);
  }

  for (auto &v : meshlet_vertices) {
    v += base_vertex;
  }
  return MeshletLoadResult{.meshlets = std::move(meshlets),
                           .meshlet_vertices = std::move(meshlet_vertices),
                           .meshlet_triangles = std::move(meshlet_triangles)};
}

void free_ktx_texture(void *ktx_tex) {
  if (ktx_tex) {
    ktxTexture2_Destroy((ktxTexture2 *)ktx_tex);
  }
}

}  // namespace

namespace model {

bool load_model(const std::filesystem::path &path, const glm::mat4 &root_transform,
                ModelInstance &out_model, ModelLoadResult &out_load_result) {
  ZoneScoped;
  out_load_result = {};
  out_model = {};
  const cgltf_options gltf_load_opts{};
  cgltf_data *raw_gltf{};
  cgltf_result gltf_res = cgltf_parse_file(&gltf_load_opts, path.c_str(), &raw_gltf);
  std::unique_ptr<cgltf_data, void (*)(cgltf_data *)> gltf(raw_gltf, cgltf_free);
  std::filesystem::path directory_path = path.parent_path();

  if (gltf_res != cgltf_result_success) {
    if (gltf_res == cgltf_result_file_not_found) {
      return false;
    }
    return false;
  }

  gltf_res = cgltf_load_buffers(&gltf_load_opts, gltf.get(), path.c_str());

  if (gltf_res != cgltf_result_success) {
    return false;
  }

  auto &texture_uploads = out_load_result.texture_uploads;
  texture_uploads.resize(gltf->images_count);

  auto load_img = [&](uint32_t gltf_img_i, rhi::TextureFormat format) -> uint32_t {
    const cgltf_image &img = gltf->images[gltf_img_i];
    const std::filesystem::path full_img_path = directory_path / img.uri;
    if (!img.buffer_view) {
      if (full_img_path.extension() == ".ktx2") {
        auto load_result = gfx::load_ktx_texture(full_img_path);
        auto *ktx_tex = load_result.texture;
        const rhi::TextureDesc desc{
            // TODO: fix
            .format = rhi::TextureFormat::R8G8B8A8Unorm,
            .storage_mode = rhi::StorageMode::Default,
            .usage = rhi::TextureUsageSample,
            .dims = glm::uvec3{ktx_tex->baseWidth, ktx_tex->baseHeight, ktx_tex->baseDepth},
            .mip_levels = ktx_tex->numLevels,
            .array_length = 1,
            .bindless = true};
        if (!ktx_tex) {
          ASSERT(0);
        }
        if (ktx_tex->numLevels == 0) {
          ASSERT(0);
        }
        if (ktx_tex->vkFormat != VK_FORMAT_ASTC_4x4_SRGB_BLOCK &&
            ktx_tex->vkFormat != VK_FORMAT_ASTC_4x4_UNORM_BLOCK) {
          ASSERT(0 && "unhandled format");
        }

        auto &upload = texture_uploads[gltf_img_i];
        upload.data = std::unique_ptr<void, UntypedDeleterFuncPtr>(ktx_tex, &free_ktx_texture);
        upload.desc = desc;
        int blocks_wide = align_divide_up(ktx_tex->baseWidth, 4);
        int blocks_tall = align_divide_up(ktx_tex->baseHeight, 4);
        int src_bytes_per_row = blocks_wide * 16;
        upload.bytes_per_row = src_bytes_per_row;
        upload.load_type = CPUTextureLoadType::Ktx2;
        upload.desc.format = load_result.format;
        upload.compressed_blocks_tall = blocks_tall;
      } else {
        int w{}, h{}, comp{};
        uint8_t *data = stbi_load(full_img_path.c_str(), &w, &h, &comp, 4);
        const uint32_t mip_levels = math::get_mip_levels(w, h);
        const rhi::TextureDesc desc{.format = rhi::TextureFormat::R8G8B8A8Unorm,
                                    .storage_mode = rhi::StorageMode::Default,
                                    .usage = rhi::TextureUsageSample,
                                    .dims = glm::uvec3{w, h, 1},
                                    .mip_levels = mip_levels,
                                    .array_length = 1,
                                    .bindless = true};
        if (!data) {
          ASSERT(0);
        }
        auto &upload = texture_uploads[gltf_img_i];
        upload.data = std::unique_ptr<void, UntypedDeleterFuncPtr>(data, &stbi_image_free);
        upload.desc = desc;
        upload.load_type = CPUTextureLoadType::StbImage;
        upload.bytes_per_row = desc.dims.x * 4;
        upload.desc.format = format;
      }
    } else {
      ASSERT(0 && "need to handle yet");
    }
    return gltf_img_i;
  };

  {
    ZoneScopedN("Load materials and images");
    auto &materials = out_load_result.materials;
    materials.resize(gltf->materials_count);
    std::vector<std::future<void>> img_load_futures;
    img_load_futures.reserve(gltf->materials_count);
    for (size_t material_i = 0; material_i < gltf->materials_count; material_i++) {
      img_load_futures.emplace_back(ThreadPool::get().submit_task([&, material_i]() {
        const cgltf_material *gltf_mat = &gltf->materials[material_i];
        if (!gltf_mat) {
          ALWAYS_ASSERT(0);
        }
        Material material{};
        material.albedo_factors.r = gltf_mat->pbr_metallic_roughness.base_color_factor[0];
        material.albedo_factors.g = gltf_mat->pbr_metallic_roughness.base_color_factor[1];
        material.albedo_factors.b = gltf_mat->pbr_metallic_roughness.base_color_factor[2];
        material.albedo_factors.a = gltf_mat->pbr_metallic_roughness.base_color_factor[3];
        material.albedo_factors.a = 1.0;

        auto set_and_load_material_img = [&gltf, &load_img, &texture_uploads](
                                             const cgltf_texture_view *tex_view,
                                             uint32_t &result_tex_id, rhi::TextureFormat format) {
          if (!tex_view || !tex_view->texture) {
            return;
          }
          size_t gltf_image_i{};
          if (tex_view->texture->has_basisu && tex_view->texture->basisu_image) {
            gltf_image_i = tex_view->texture->basisu_image - gltf->images;
          } else if (tex_view->texture->image) {
            gltf_image_i = tex_view->texture->image - gltf->images;
          } else {
            LINFO("No texture image found");
            return;
          }
          if (texture_uploads[gltf_image_i].data) {
            result_tex_id = gltf_image_i;
          } else {
            result_tex_id = load_img(gltf_image_i, format);
          }
        };
        set_and_load_material_img(&gltf_mat->pbr_metallic_roughness.base_color_texture,
                                  material.albedo_tex, rhi::TextureFormat::R8G8B8A8Srgb);
        materials[material_i] = material;
      }));
    }

    for (auto &fut : img_load_futures) {
      fut.get();
    }
  }

  std::vector<DefaultVertex> &all_vertices = out_load_result.vertices;
  std::vector<rhi::DefaultIndexT> &all_indices = out_load_result.indices;

  size_t model_vertex_count{};
  size_t model_index_count{};
  size_t model_mesh_count{};
  for (uint32_t mesh_i = 0; mesh_i < gltf->meshes_count; mesh_i++) {
    const cgltf_mesh &mesh = gltf->meshes[mesh_i];
    model_mesh_count += mesh.primitives_count;
    for (uint32_t prim_i = 0; prim_i < mesh.primitives_count; prim_i++) {
      const cgltf_primitive &prim = mesh.primitives[prim_i];
      model_vertex_count += prim.attributes[0].data->count;
      if (prim.indices) {
        model_index_count += prim.indices->count;
      }
    }
  }

  all_vertices.reserve(model_vertex_count);
  all_indices.reserve(model_index_count);

  std::vector<Mesh> &meshes = out_load_result.meshes;
  meshes.reserve(model_mesh_count);

  std::vector<uint32_t> gltf_node_to_node_i;
  gltf_node_to_node_i.resize(gltf->nodes_count, UINT32_MAX);

  std::vector<std::vector<uint32_t>> primitive_mesh_indices_per_mesh;
  primitive_mesh_indices_per_mesh.resize(gltf->meshes_count);

  {  // process mesh/primitive
    ZoneScopedN("Process meshes");
    uint32_t overall_mesh_i = 0;
    size_t total_indices = 0;
    size_t total_vertices = 0;

    for (uint32_t mesh_i = 0; mesh_i < gltf->meshes_count; mesh_i++) {
      const auto &mesh = gltf->meshes[mesh_i];
      for (uint32_t prim_i = 0; prim_i < mesh.primitives_count; prim_i++, overall_mesh_i++) {
        primitive_mesh_indices_per_mesh[mesh_i].push_back(overall_mesh_i);
        const cgltf_primitive &primitive = mesh.primitives[prim_i];
        const auto base_vertex = total_vertices;
        const size_t vertex_offset = base_vertex * sizeof(DefaultVertex);
        uint32_t index_offset = UINT32_MAX;
        uint32_t index_count = UINT32_MAX;
        ASSERT(primitive.indices);
        if (primitive.indices) {
          index_count = primitive.indices->count;
          index_offset = total_indices * sizeof(rhi::DefaultIndexT);
          total_indices += index_count;
        }
        ASSERT(primitive.attributes[0].data);
        const auto vertex_count = static_cast<uint32_t>(primitive.attributes[0].data->count);
        total_vertices += vertex_count;
        const uint32_t material_idx =
            primitive.material ? primitive.material - gltf->materials : UINT32_MAX;

        meshes.push_back(Mesh{
            .vertex_offset_bytes = static_cast<uint32_t>(vertex_offset),
            .index_offset = index_offset,
            .vertex_count = vertex_count,
            .index_count = index_count,
            .material_id = material_idx,
        });
      }
    }

    all_indices.resize(total_indices);
    all_vertices.resize(total_vertices);

    std::vector<std::future<void>> mesh_load_futures;
    auto total_meshes = overall_mesh_i;
    mesh_load_futures.reserve(total_meshes);
    overall_mesh_i = 0;

    for (uint32_t mesh_i = 0; mesh_i < gltf->meshes_count; mesh_i++) {
      for (uint32_t prim_i = 0; prim_i < gltf->meshes[mesh_i].primitives_count;
           prim_i++, overall_mesh_i++) {
        const uint32_t overall_prim_i = overall_mesh_i;
        mesh_load_futures.emplace_back(ThreadPool::get().submit_task(
            [mesh_i, prim_i, overall_prim_i, &gltf, &all_indices, &meshes, &all_vertices]() {
              ZoneScopedN("Process Mesh");
              const auto &primitive = gltf->meshes[mesh_i].primitives[prim_i];
              auto &result_mesh = meshes[overall_prim_i];
              size_t base_index = result_mesh.index_offset / sizeof(rhi::DefaultIndexT);
              for (size_t i = 0; i < primitive.indices->count; i++) {
                all_indices[base_index + i] = cgltf_accessor_read_index(primitive.indices, i);
              }

              auto base_vertex =
                  static_cast<size_t>(result_mesh.vertex_offset_bytes / sizeof(DefaultVertex));
              glm::vec3 tot_center{};
              for (size_t attr_i = 0; attr_i < primitive.attributes_count; attr_i++) {
                const auto &attr = primitive.attributes[attr_i];
                const cgltf_accessor *accessor = attr.data;
                if (attr.type == cgltf_attribute_type_position) {
                  for (size_t i = 0; i < accessor->count; i++) {
                    float pos[3] = {0, 0, 0};
                    cgltf_accessor_read_float(accessor, i, pos, 3);
                    all_vertices[base_vertex + i].pos = glm::vec4{pos[0], pos[1], pos[2], 0};
                    tot_center += glm::vec3{pos[0], pos[1], pos[2]};
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
                    all_vertices[base_vertex + i].normal =
                        glm::vec3{normal[0], normal[1], normal[2]};
                  }
                }
              }

              auto vertex_count = result_mesh.vertex_count;
              glm::vec3 center = tot_center / glm::vec3{static_cast<float>(vertex_count)};
              float radius{};
              for (size_t i = base_vertex; i < base_vertex + vertex_count; i++) {
                radius = glm::max(radius, glm::distance(glm::vec3{all_vertices[i].pos}, center));
              }
              result_mesh.center = center;
              result_mesh.radius = radius;
            }));
      }
    }

    for (auto &fut : mesh_load_futures) {
      fut.get();
    }

    auto &meshlet_datas = out_load_result.meshlet_process_result.meshlet_datas;
    meshlet_datas.reserve(model_vertex_count / k_max_vertices_per_meshlet);
    std::filesystem::path meshlet_cache_path =
        std::filesystem::path(path).replace_extension(".meshletcache");
    if (std::filesystem::exists(meshlet_cache_path)) {
      std::ifstream meshlet_cache_file(meshlet_cache_path, std::ios::binary);
      read_meshlets(meshlet_cache_file, meshlet_datas);
    } else {
      meshlet_datas.resize(meshes.size());
      std::vector<std::future<void>> meshlet_load_futures;
      meshlet_load_futures.reserve(meshes.size());
      ;
      size_t mesh_i = 0;
      for (const Mesh &mesh : meshes) {
        const uint32_t base_vertex = mesh.vertex_offset_bytes / sizeof(DefaultVertex);
        meshlet_load_futures.emplace_back(ThreadPool::get().submit_task(
            [&all_vertices, &all_indices, &meshlet_datas, mesh_i, base_vertex, &meshes]() {
              ZoneScopedN("Process Meshlet");
              auto &mesh = meshes[mesh_i];
              meshlet_datas[mesh_i] = load_meshlet_data(
                  std::span(&all_vertices[base_vertex], mesh.vertex_count),
                  std::span(&all_indices[mesh.index_offset / sizeof(rhi::DefaultIndexT)],
                            mesh.index_count),
                  base_vertex);
            }));
        mesh_i++;
      }

      for (auto &fut : meshlet_load_futures) {
        fut.get();
      }
    }

    if (!std::filesystem::exists(meshlet_cache_path)) {
      std::ofstream meshlet_cache_file(meshlet_cache_path, std::ios::binary);
      write_meshlets(meshlet_cache_file, std::span<const MeshletLoadResult>(meshlet_datas.data(),
                                                                            meshlet_datas.size()));
    }
  }

  {
    ZoneScopedN("Process nodes");
    auto &model = out_model;
    model.global_transforms.reserve(gltf->nodes_count);
    model.local_transforms.reserve(gltf->nodes_count);
    model.nodes.reserve(gltf->nodes_count);
    model.mesh_ids.reserve(gltf->nodes_count);
    // model.changed_this_frame.resize(ModelInstance::k_max_hierarchy_depth);
    // process nodes
    struct AddNodeStackItem {
      uint32_t gltf_node_i;
      int32_t parent_node;
    };

    assert(gltf->scenes_count == 1);
    const auto *scene = &gltf->scenes[0];
    // add root node
    glm::vec3 root_translation;
    glm::quat root_rotation;
    glm::vec3 root_scale_vec;
    math::decompose_matrix(&root_transform[0][0], root_translation, root_rotation, root_scale_vec);
    float root_scale = glm::max(root_scale_vec.x, glm::max(root_scale_vec.y, root_scale_vec.z));
    const auto root_node =
        add_node_to_model(model, Hierarchy::k_invalid_node_id, 0, root_translation, root_rotation,
                          root_scale, Mesh::k_invalid_mesh_id);

    std::stack<AddNodeStackItem> gltf_node_stack;
    for (uint32_t i = 0; i < scene->nodes_count; i++) {
      gltf_node_stack.push(
          AddNodeStackItem{.gltf_node_i = static_cast<uint32_t>(scene->nodes[i] - gltf->nodes),
                           .parent_node = root_node});
    }

    uint32_t tot_mesh_nodes{};
    while (!gltf_node_stack.empty()) {
      auto [gltf_node_i, parent_node] = gltf_node_stack.top();
      gltf_node_stack.pop();
      const cgltf_node &gltf_node = gltf->nodes[gltf_node_i];
      glm::vec3 translation;
      glm::quat rotation;
      glm::vec3 scale_vec;
      if (gltf_node.has_matrix) {
        math::decompose_matrix(gltf_node.matrix, translation, rotation, scale_vec);
      } else {
        translation = {gltf_node.translation[0], gltf_node.translation[1],
                       gltf_node.translation[2]};
        // TODO: is this flipped
        rotation = {gltf_node.rotation[3], gltf_node.rotation[0], gltf_node.rotation[1],
                    gltf_node.rotation[2]};
        scale_vec = {gltf_node.scale[0], gltf_node.scale[1], gltf_node.scale[2]};
      }
      float scale = glm::max(scale_vec.x, glm::max(scale_vec.y, scale_vec.z));
      const int32_t new_node =
          add_node_to_model(model, parent_node, model.nodes[parent_node].level + 1, translation,
                            rotation, scale, Mesh::k_invalid_mesh_id);
      gltf_node_to_node_i[gltf_node_i] = new_node;

      if (gltf_node.mesh) {
        const int32_t child_level = model.nodes[parent_node].level + 1;
        const auto mesh_id = gltf_node.mesh - gltf->meshes;
        for (auto prim_mesh_id : primitive_mesh_indices_per_mesh[mesh_id]) {
          add_node_to_model(model, new_node, child_level, glm::vec3{}, glm::identity<glm::quat>(),
                            1.0, prim_mesh_id);
          tot_mesh_nodes++;
        }
      }

      for (uint32_t ci = 0; ci < gltf_node.children_count; ci++) {
        const auto child_gltf_node_i = static_cast<uint32_t>(gltf_node.children[ci] - gltf->nodes);
        gltf_node_stack.push(
            AddNodeStackItem{.gltf_node_i = child_gltf_node_i, .parent_node = new_node});
      }
    }

    model.tot_mesh_nodes = tot_mesh_nodes;
    ASSERT(model.changed_this_frame.size() > 0);
    model.mark_changed(0);
    model.update_transforms();
  }

  {  // process meshlet for entire instance
    ZoneScopedN("Process Meshlets");
    auto &meshlet_process_result = out_load_result.meshlet_process_result;
    auto &meshlet_datas = out_load_result.meshlet_process_result.meshlet_datas;
    size_t i = 0;
    for (auto &meshlet_data : meshlet_datas) {
      meshlet_data.meshlet_triangles_offset = meshlet_process_result.tot_meshlet_tri_count;
      meshlet_data.meshlet_vertices_offset = meshlet_process_result.tot_meshlet_verts_count;
      meshlet_data.meshlet_base = meshlet_process_result.tot_meshlet_count;
      out_load_result.meshes[i++].meshlet_count = meshlet_data.meshlets.size();
      meshlet_process_result.tot_meshlet_count += meshlet_data.meshlets.size();
      meshlet_process_result.tot_meshlet_verts_count += meshlet_data.meshlet_vertices.size();
      meshlet_process_result.tot_meshlet_tri_count += meshlet_data.meshlet_triangles.size();
    }
  }

  return true;
}

}  // namespace model
