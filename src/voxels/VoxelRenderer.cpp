#include "VoxelRenderer.hpp"

#include <stb_image/stb_image.h>

#include <Metal/MTLRenderCommandEncoder.hpp>
#include <Metal/MTLRenderPipeline.hpp>
#include <Metal/Metal.hpp>
#include <unordered_set>

#include "chunk_shaders_shared.h"
#include "core/MathUtil.hpp"
#include "core/ThreadPool.hpp"
#include "gfx/GFXTypes.hpp"
#include "gfx/RendererMetal.hpp"
#include "gfx/metal/MetalUtil.hpp"
#include "voxels/Types.hpp"
#include "voxels/VoxelDB.hpp"

namespace vox {

void vox::Renderer::upload_chunk(const ChunkUploadData& upload_data,
                                 const std::vector<uint64_t>& vertices) {
  ChunkKey key = upload_data.key;
  auto handle = upload_data.handle;
  NS::SharedPtr<MTL::Buffer> vertex_buf;
  if (!vertices.size()) {
    return;
  }
  glm::vec3 chunk_world_pos = glm::vec3{key} * glm::vec3{k_chunk_len};
  size_t quads_size_bytes = sizeof(uint64_t) * upload_data.quad_count;
  auto vertex_handle = device_->create_buf_h(
      rhi::BufferDesc{.storage_mode = rhi::StorageMode::Default, .size = quads_size_bytes});
  memcpy(device_->get_buf(vertex_handle)->contents(), vertices.data(), quads_size_bytes);
  chunk_render_datas_.emplace(handle.to64(), ChunkRenderData{
                                                 .vertex_handle = std::move(vertex_handle),
                                                 .chunk_world_pos = chunk_world_pos,
                                                 .quad_count = upload_data.quad_count,
                                                 .face_vert_begin = upload_data.face_vert_begin,
                                                 .face_vert_length = upload_data.face_vert_length,
                                             });
}

void Renderer::encode_gbuffer_pass(MTL::RenderCommandEncoder* enc, MTL::Buffer* uniform_buf) {
  MTL::Buffer* index_buf = get_mtl_buf(index_buf_);
  enc->setRenderPipelineState(main_pso_);
  enc->setVertexBuffer(uniform_buf, 0, 2);
  enc->setFragmentBuffer(get_mtl_buf(voxel_material_buf_), 0, 0);
  // TODO: arg encoder
  enc->setFragmentTexture(
      reinterpret_cast<MetalTexture*>(renderer_->get_device()->get_tex(voxel_tex_arr_))->texture(),
      0);
  for (const auto& [key, val] : chunk_render_datas_) {
    auto* buf = get_mtl_buf(val.vertex_handle);
    for (int face = 0; face < 6; face++) {
      if (val.face_vert_length[face]) {
        PerChunkUniforms chunk_uniforms{.chunk_pos = glm::ivec4{val.chunk_world_pos, face}};
        enc->setVertexBuffer(buf, val.face_vert_begin[face] * sizeof(uint64_t), 0);
        enc->setVertexBytes(&chunk_uniforms, sizeof(chunk_uniforms), 1);
        enc->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, val.face_vert_length[face] * 6ul,
                                   MTL::IndexTypeUInt32, index_buf, 0);
      }
    }
  }
}

void Renderer::init(RendererMetal* renderer) {
  renderer_ = renderer;
  device_ = renderer_->get_device();
  {  // create index buffer to use for all chunks
    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(k_chunk_len_cu) * 6);
    for (int i = 0, vert_i = 0; i < k_chunk_len_cu * 6; i++, vert_i += 4) {
      indices.emplace_back(vert_i + 2);
      indices.emplace_back(vert_i + 0);
      indices.emplace_back(vert_i + 1);
      indices.emplace_back(vert_i + 1);
      indices.emplace_back(vert_i + 3);
      indices.emplace_back(vert_i + 2);
    }
    size_t copy_size = sizeof(uint32_t) * indices.size();
    index_buf_ = device_->create_buf_h(rhi::BufferDesc{.size = copy_size});
    memcpy(device_->get_buf(index_buf_)->contents(), indices.data(), copy_size);
  }

  renderer_->add_main_render_pass_callback(
      [this](MTL::RenderCommandEncoder* enc, MTL::Buffer* uniform_buf) {
        encode_gbuffer_pass(enc, uniform_buf);
      });

  MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
  desc->setVertexFunction(renderer_->get_function("chunk_vertex_main"));
  desc->setFragmentFunction(renderer_->get_function("chunk_fragment_main"));
  desc->setLabel(util::mtl::string("chunk pipeline"));
  desc->colorAttachments()->object(0)->setPixelFormat(renderer_->main_pixel_format);
  desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);
  main_pso_ = renderer_->load_pipeline(desc);
}

void Renderer::load_voxel_resources(VoxelDB& vdb, const std::filesystem::path& block_tex_dir) {
  {  // textures
    std::unordered_set<std::string> to_load_tex_filenames;
    to_load_tex_filenames.reserve(vdb.get_block_texture_datas().size());
    for (const BlockTextureData& tex_data : vdb.get_block_texture_datas()) {
      to_load_tex_filenames.insert(tex_data.albedo_texname);
      to_load_tex_filenames.insert(tex_data.albedo_texname.stem().string() + "_n" +
                                   tex_data.albedo_texname.extension().string());
    }

    if (to_load_tex_filenames.size()) {
      std::vector<std::string> to_load_tex_filenames_vec{to_load_tex_filenames.begin(),
                                                         to_load_tex_filenames.end()};

      std::unordered_map<std::string, uint32_t> block_tex_name_to_arr_idx;
      block_tex_name_to_arr_idx.reserve(to_load_tex_filenames_vec.size());
      for (size_t i = 0; i < to_load_tex_filenames_vec.size(); i++) {
        block_tex_name_to_arr_idx[to_load_tex_filenames_vec[i]] = i;
      }
      vdb.populate_tex_arr_indices(block_tex_name_to_arr_idx);

      struct LoadData {
        int x{}, y{}, comp{};
        uint8_t* data;
      };

      std::vector<LoadData> tex_array_load_datas(to_load_tex_filenames_vec.size());

      auto tex_load_fut = ThreadPool::get().submit_loop(
          0ull, to_load_tex_filenames_vec.size(),
          [&to_load_tex_filenames_vec, &tex_array_load_datas, &block_tex_dir](size_t tex_i) {
            LoadData ld{};
            std::filesystem::path tex_path = block_tex_dir / to_load_tex_filenames_vec[tex_i];
            ld.data = stbi_load(tex_path.c_str(), &ld.x, &ld.y, &ld.comp, 4);
            ld.comp = 4;
            if (!ld.data) {
              LERROR("Failed to load texture data at: {}", tex_path.string());
            }
            tex_array_load_datas[tex_i] = ld;
          });

      tex_load_fut.get();

      int x0 = tex_array_load_datas[0].x;
      int y0 = tex_array_load_datas[0].y;
      int comp0 = tex_array_load_datas[0].comp;
      bool valid_tex_array{true};
      TextureArrayUpload upload{};

      upload.data.reserve(tex_array_load_datas.size());
      for (const auto& ld : tex_array_load_datas) {
        if (ld.x != x0 || ld.y != y0 || ld.comp != comp0) {
          LERROR("Mismatch texture array dimensions:\tA: {} {} {}\tB: {} {} {}\n", x0, y0, comp0,
                 ld.x, ld.y, ld.comp);
          valid_tex_array = false;
          break;
        }
        upload.data.emplace_back(ld.data);
      }

      if (valid_tex_array) {
        const rhi::TextureDesc desc{
            .format = rhi::TextureFormat::R8G8B8A8Unorm,
            .storage_mode = rhi::StorageMode::GPUOnly,
            .dims = {x0, y0, 1},
            .mip_levels = static_cast<uint32_t>(math::get_mip_levels(x0, y0)),
            .array_length = static_cast<uint32_t>(tex_array_load_datas.size())};
        upload.bytes_per_row = x0 * 4;
        upload.dims = {x0, y0, 1};
        upload.cpu_type = CPUTextureLoadType::StbImage;
        voxel_tex_arr_ = renderer_->get_device()->create_tex_h(desc);
        upload.tex = voxel_tex_arr_.handle;

        renderer_->load_tex_array(std::move(upload));
      }
    }
  }

  {  // materials
    const auto& voxel_datas = vdb.get_block_datas();
    std::vector<VoxelMaterial> materials;
    materials.reserve(voxel_datas.size());
    for (const auto& d : voxel_datas) {
      auto& mat = materials.emplace_back();
      for (int i = 0; i < 6; i++) {
        mat.indices[i] = d.albedo_tex_idx[i];
        mat.indices[i + 6] = d.normal_tex_idx[i];
      }
    }

    size_t copy_size = sizeof(VoxelMaterial) * materials.size();
    voxel_material_buf_ = renderer_->get_device()->create_buf_h(rhi::BufferDesc{.size = copy_size});
    memcpy(renderer_->get_device()->get_buf(voxel_material_buf_)->contents(), materials.data(),
           copy_size);
  }
}

}  // namespace vox
