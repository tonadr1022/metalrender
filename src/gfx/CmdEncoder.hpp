#pragma once

#include "gfx/GFXTypes.hpp"
#include "gfx/RendererTypes.hpp"  // IWYU pragma: keep

namespace rhi {

enum PipelineStage : uint64_t {
  PipelineStage_None = 0,
  PipelineStage_TopOfPipe = 0x1ull,
  PipelineStage_DrawIndirect = 0x2ull,
  PipelineStage_VertexInput = 0x4ull,
  PipelineStage_VertexShader = 0x8ull,
  PipelineStage_FragmentShader = 0x40ull,
  PipelineStage_EarlyFragmentTests = 0x100ull,
  PipelineStage_LateFragmentTests = 0x200ull,
  PipelineStage_ColorAttachmentOutput = 0x400ull,
  PipelineStage_ComputeShader = 0x800ull,
  PipelineStage_AllTransfer = 0x1000ull,
  PipelineStage_BottomOfPipe = 0x2000ull,
  PipelineStage_Host = 0x4000ull,
  PipelineStage_AllGraphics = 0x8000ull,
  PipelineStage_AllCommands = 0x10000ull,
};

enum AccessFlags : uint64_t {
  AccessFlags_None = 0ULL,
  AccessFlags_IndirectCommandRead = 0X00000001ULL,
  AccessFlags_IndexRead = 0X00000002ULL,
  AccessFlags_VertexAttributeRead = 0X00000004ULL,
  AccessFlags_UniformRead = 0X00000008ULL,
  AccessFlags_InputAttachmentRead = 0X00000010ULL,
  AccessFlags_ShaderRead = 0X00000020ULL,
  AccessFlags_ShaderWrite = 0X00000040ULL,
  AccessFlags_ColorAttachmentRead = 0X00000080ULL,
  AccessFlags_ColorAttachmentWrite = 0X00000100ULL,
  AccessFlags_DepthStencilRead = 0X00000200ULL,
  AccessFlags_DepthStencilWrite = 0X00000400ULL,
  AccessFlags_TransferRead = 0X00000800ULL,
  AccessFlags_TransferWrite = 0X00001000ULL,
  AccessFlags_HostRead = 0X00002000ULL,
  AccessFlags_HostWrite = 0X00004000ULL,
  AccessFlags_MemoryRead = 0X00008000ULL,
  AccessFlags_MemoryWrite = 0X00010000ULL,
  AccessFlags_ShaderSampledRead = 0X100000000ULL,
  AccessFlags_ShaderStorageRead = 0X200000000ULL,
  AccessFlags_ShaderStorageWrite = 0X400000000ULL,
};

class CmdEncoder {
 public:
  virtual void begin_rendering(std::initializer_list<RenderingAttachmentInfo> attachments) = 0;
  virtual void bind_pipeline(PipelineHandle handle) = 0;
  void bind_pipeline(const PipelineHandleHolder& handle) { bind_pipeline(handle.handle); }

  virtual void draw_primitives(PrimitiveTopology topology, size_t vertex_start, size_t count,
                               size_t instance_count) = 0;
  void draw_primitives(PrimitiveTopology topology, size_t vertex_start, size_t count) {
    draw_primitives(topology, vertex_start, count, 1);
  }
  void draw_primitives(PrimitiveTopology topology, size_t count) {
    draw_primitives(topology, 0, count, 1);
  }

  virtual void draw_indexed_primitives(PrimitiveTopology topology, BufferHandle index_buf,
                                       size_t index_start, size_t count, size_t instance_count,
                                       size_t base_vertex, size_t base_instance) = 0;
  void draw_indexed_primitives(PrimitiveTopology topology, BufferHandle index_buf,
                               size_t index_start, size_t count) {
    draw_indexed_primitives(topology, index_buf, index_start, count, 1, 0, 0);
  }
  virtual void set_depth_stencil_state(CompareOp depth_compare_op, bool depth_write_enabled) = 0;
  virtual void set_wind_order(WindOrder wind_order) = 0;
  virtual void set_cull_mode(CullMode cull_mode) = 0;

  virtual void push_constants(void* data, size_t size) = 0;
  CmdEncoder() = default;
  virtual ~CmdEncoder() = default;
  virtual void end_encoding() = 0;
  virtual void set_viewport(glm::uvec2 min, glm::uvec2 extent) = 0;

  virtual void upload_texture_data(rhi::BufferHandle src_buf, size_t src_offset,
                                   size_t src_bytes_per_row, rhi::TextureHandle dst_tex) = 0;
  virtual void copy_tex_to_buf(rhi::TextureHandle src_tex, size_t src_slice, size_t src_level,
                               rhi::BufferHandle dst_buf, size_t dst_offset) = 0;
  // virtual void bind_index_buf(rhi::BufferHandle index_buf, size_t offset) = 0;
  // virtual void bind_index_buf(rhi::BufferHandle index_buf) { bind_index_buf(index_buf, 0); }

  virtual void prepare_indexed_indirect_draws(rhi::BufferHandle indirect_buf, size_t offset,
                                              size_t draw_cnt, rhi::BufferHandle index_buf,
                                              size_t index_buf_offset) = 0;

  virtual void barrier(PipelineStage src_stage, AccessFlags src_access, PipelineStage dst_stage,
                       AccessFlags dst_access) = 0;
  virtual void draw_indexed_indirect(rhi::BufferHandle indirect_buf, size_t offset,
                                     size_t draw_cnt) = 0;
};

}  // namespace rhi
