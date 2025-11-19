#pragma once

#include <Metal/MTLCommandEncoder.hpp>
#include <Metal/MTLGPUAddress.hpp>

#include "gfx/CmdEncoder.hpp"

class MetalDevice;

namespace MTL4 {

class CommandBuffer;
class RenderCommandEncoder;
class ComputeCommandEncoder;
class ArgumentTable;

}  // namespace MTL4

namespace MTL {

class ArgumentEncoder;
class Buffer;

}  // namespace MTL

struct TLAB {
  uint64_t push_constant_buf;
};
class MetalCmdEncoder : public rhi::CmdEncoder {
 public:
  MetalCmdEncoder() = default;
  MetalCmdEncoder(const MetalCmdEncoder&) = delete;
  MetalCmdEncoder(MetalCmdEncoder&&) = delete;
  MetalCmdEncoder& operator=(const MetalCmdEncoder&) = delete;
  MetalCmdEncoder& operator=(MetalCmdEncoder&&) = delete;
  ~MetalCmdEncoder() override;

  MetalCmdEncoder(MetalDevice* device, MTL4::CommandBuffer* cmd_buf);

  void begin_rendering(std::initializer_list<rhi::RenderingAttachmentInfo> attachments) override;
  void end_encoding() override;
  void bind_pipeline(rhi::PipelineHandle handle) override;
  void set_viewport(glm::uvec2 min, glm::uvec2 extent) override;
  void draw_primitives(rhi::PrimitiveTopology topology, size_t vertex_start, size_t count,
                       size_t instance_count) override;
  void push_constants(void* data, size_t size) override;
  void draw_indexed_primitives(rhi::PrimitiveTopology topology, rhi::BufferHandle index_buf,
                               size_t index_start, size_t count, size_t instance_count,
                               size_t base_vertex, size_t base_instance) override;
  void set_depth_stencil_state(rhi::CompareOp depth_compare_op, bool depth_write_enabled) override;
  void set_wind_order(rhi::WindOrder wind_order) override;
  void set_cull_mode(rhi::CullMode cull_mode) override;
  // TODO: will this work with vulkanisms
  void upload_texture_data(rhi::BufferHandle src_buf, size_t src_offset, size_t src_bytes_per_row,
                           rhi::TextureHandle dst_tex) override;
  void copy_tex_to_buf(rhi::TextureHandle src_tex, size_t src_slice, size_t src_level,
                       rhi::BufferHandle dst_buf, size_t dst_offset) override;

  // must call push_constants AND bind_index_buf first
  uint32_t prepare_indexed_indirect_draws(rhi::BufferHandle indirect_buf, size_t offset,
                                          size_t draw_cnt, rhi::BufferHandle index_buf,
                                          size_t index_buf_offset, void* push_constant_data,
                                          size_t push_constant_size) override;
  // void bind_index_buf(rhi::BufferHandle index_buf, size_t offset) override {
  //   curr_bound_index_buf_ = index_buf;
  //   curr_bound_index_buf_offset_ = offset;
  // }
  void barrier(rhi::PipelineStage src_stage, rhi::AccessFlags src_access,
               rhi::PipelineStage dst_stage, rhi::AccessFlags dst_access) override;

  void draw_indexed_indirect(rhi::BufferHandle indirect_buf, uint32_t indirect_buf_id,
                             size_t offset, size_t draw_cnt) override;

 private:
  void init_icb_arg_encoder_and_buf();
  void flush_compute_barriers();
  void flush_render_barriers();
  void end_render_encoder();
  void end_compute_encoder();
  void start_compute_encoder();

  MetalDevice* device_{};
  MTL4::CommandBuffer* cmd_buf_{};
  MTL4::RenderCommandEncoder* render_enc_{};
  MTL4::ComputeCommandEncoder* compute_enc_{};
  MTL4::ArgumentTable* arg_table_{};
  MTL::Stages compute_enc_flush_stages_{};
  MTL::Stages render_enc_flush_stages_{};
  MTL::Stages compute_enc_dst_stages_{};
  MTL::Stages render_enc_dst_stages_{};

  rhi::BufferHandle curr_bound_index_buf_;
  size_t curr_bound_index_buf_offset_;

  rhi::BufferHandleHolder main_icb_container_buf_;
  MTL::ArgumentEncoder* main_icb_container_arg_enc_{};

  uint8_t pc_data_[168]{};
};
