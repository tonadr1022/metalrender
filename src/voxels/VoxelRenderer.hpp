#pragma once

#include "gfx/Device.hpp"
#include "gfx/RendererTypes.hpp"
#include "gfx/metal/MetalBuffer.hpp"
#include "voxels/Chunk.hpp"

class RendererMetal;

class MetalBuffer;

namespace rhi {
class Device;
}

namespace MTL {
class CommandBuffer;
class RenderCommandEncoder;
class RenderPipelineState;
class Buffer;
}  // namespace MTL

namespace vox {

class Renderer {
 public:
  Renderer() = default;
  void init(RendererMetal* renderer);

  void upload_chunk(const ChunkUploadData& upload_data, const std::vector<uint64_t>& vertices);
  void encode_gbuffer_pass(MTL::RenderCommandEncoder* enc, MTL::Buffer* uniform_buf);

 private:
  struct ChunkRenderData {
    rhi::BufferHandleHolder vertex_handle;
    glm::ivec3 chunk_world_pos;
    uint32_t quad_count;
    std::array<uint32_t, 6> face_vert_begin{};
    std::array<uint32_t, 6> face_vert_length{};
  };

  std::unordered_map<uint64_t, ChunkRenderData> chunk_render_datas_;
  rhi::BufferHandleHolder index_buf_;
  RendererMetal* renderer_{};
  rhi::Device* device_;
  MTL::RenderPipelineState* main_pso_{};

  MTL::Buffer* get_mtl_buf(const rhi::BufferHandleHolder& handle) {
    return reinterpret_cast<MetalBuffer*>(device_->get_buf(handle))->buffer();
  }
};

}  // namespace vox
