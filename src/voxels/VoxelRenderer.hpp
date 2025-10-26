#pragma once

#include <filesystem>

#include "gfx/Device.hpp"
#include "gfx/RendererTypes.hpp"
#include "gfx/metal/MetalBuffer.hpp"
#include "voxels/Chunk.hpp"
#include "voxels/Types.hpp"

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

class VoxelDB;

class Renderer {
 public:
  Renderer() = default;
  void init(RendererMetal* renderer);
  void on_imgui();

  void upload_chunk(const ChunkUploadData& upload_data);
  void encode_gbuffer_pass(MTL::RenderCommandEncoder* enc, MTL::Buffer* uniform_buf);
  void load_voxel_resources(VoxelDB& vdb, const std::filesystem::path& block_tex_dir);

 private:
  struct ChunkRenderData {
    rhi::BufferHandleHolder vertex_handle;
    glm::ivec3 chunk_world_pos;
    struct PerLod {
      uint32_t quad_count;
      std::array<uint32_t, 6> face_vert_begin{};
      std::array<uint32_t, 6> face_vert_length{};
      size_t vert_begin_bytes{};
    };
    std::array<PerLod, k_chunk_bits + 1> lods;
  };

  std::unordered_map<uint64_t, ChunkRenderData> chunk_render_datas_;
  RendererMetal* renderer_{};
  rhi::Device* device_;
  MTL::RenderPipelineState* main_pso_{};

  MTL::Buffer* get_mtl_buf(const rhi::BufferHandleHolder& handle) {
    return reinterpret_cast<MetalBuffer*>(device_->get_buf(handle))->buffer();
  }

  rhi::BufferHandleHolder index_buf_;
  rhi::TextureHandleHolder voxel_tex_arr_;
  rhi::BufferHandleHolder voxel_material_buf_;
  bool normal_map_enabled_{true};
  int curr_render_lod_{0};
};

}  // namespace vox
