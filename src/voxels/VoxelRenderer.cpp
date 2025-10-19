#include "VoxelRenderer.hpp"

#include <Metal/MTLRenderCommandEncoder.hpp>
#include <Metal/MTLRenderPipeline.hpp>
#include <Metal/Metal.hpp>

#include "chunk_shaders_shared.h"
#include "core/EAssert.hpp"
#include "gfx/GFXTypes.hpp"
#include "gfx/RendererMetal.hpp"
#include "gfx/metal/MetalUtil.hpp"
#include "voxels/TerrainGenerator.hpp"
#include "voxels/Types.hpp"

namespace vox {

void vox::Renderer::upload_chunk(ChunkHandle handle, ChunkKey key, Chunk&,
                                 const PaddedChunkVoxArr& padded_blocks) {
  std::vector<VoxelVertex> vertices;
  MeshResult result{.vertices = vertices};
  populate_mesh(padded_blocks, result);
  NS::SharedPtr<MTL::Buffer> vertex_buf;
  if (!vertices.size()) {
    return;
  }
  glm::vec3 chunk_world_pos = glm::vec3{key} * glm::vec3{k_chunk_len};
  size_t vertices_size_bytes = sizeof(VoxelVertex) * result.vertex_count;
  auto vertex_handle = device_->create_buf_h(
      rhi::BufferDesc{.storage_mode = rhi::StorageMode::Default, .size = vertices_size_bytes});
  memcpy(device_->get_buf(vertex_handle)->contents(), vertices.data(), vertices_size_bytes);
  chunk_render_datas_.emplace(handle.to64(),
                              ChunkRenderData{.vertex_handle = std::move(vertex_handle),
                                              .vertex_count = result.vertex_count,
                                              .index_count = result.index_count,
                                              .chunk_world_pos = chunk_world_pos});
  ALWAYS_ASSERT(result.index_count < device_->get_buf(index_buf_)->size() / sizeof(uint32_t));
}

void Renderer::encode_gbuffer_pass(MTL::RenderCommandEncoder* enc, MTL::Buffer* uniform_buf) {
  MTL::Buffer* index_buf = get_mtl_buf(index_buf_);
  enc->setRenderPipelineState(main_pso_);
  for (const auto& [key, val] : chunk_render_datas_) {
    auto* buf = get_mtl_buf(val.vertex_handle);
    enc->setVertexBuffer(buf, 0, 0);

    enc->setVertexBuffer(uniform_buf, 0, 2);
    PerChunkUniforms chunk_uniforms{.chunk_pos = val.chunk_world_pos};
    enc->setVertexBytes(&chunk_uniforms, sizeof(chunk_uniforms), 1);
    enc->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, val.index_count, MTL::IndexTypeUInt32,
                               index_buf, 0);
  }
}

void Renderer::init(RendererMetal* renderer) {
  renderer_ = renderer;
  device_ = renderer_->get_device();
  {  // create index buffer to use for all chunks
    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(k_chunk_len_cu) * 6 * 6);
    for (int i = 0, vert_i = 0; i < k_chunk_len_cu * 6; i++, vert_i += 4) {
      indices.emplace_back(vert_i + 0);
      indices.emplace_back(vert_i + 1);
      indices.emplace_back(vert_i + 2);
      indices.emplace_back(vert_i + 2);
      indices.emplace_back(vert_i + 1);
      indices.emplace_back(vert_i + 3);
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
  main_pso_ = renderer_->load_pipeline(desc);
}

}  // namespace vox
