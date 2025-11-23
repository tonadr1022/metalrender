#pragma once

#include <Metal/MTLCommandEncoder.hpp>
#include <Metal/MTLGPUAddress.hpp>

#include "gfx/CmdEncoder.hpp"

class MetalDevice;

namespace MTL {

class BlitCommandEncoder;
class ComputeCommandEncoder;
class RenderCommandEncoder;
class CommandBuffer;
class ArgumentEncoder;
class Buffer;

}  // namespace MTL

class Metal3CmdEncoder : public rhi::CmdEncoder {
 public:
  Metal3CmdEncoder() = default;
  Metal3CmdEncoder(const Metal3CmdEncoder&) = delete;
  Metal3CmdEncoder(Metal3CmdEncoder&&) = delete;
  Metal3CmdEncoder& operator=(const Metal3CmdEncoder&) = delete;
  Metal3CmdEncoder& operator=(Metal3CmdEncoder&&) = delete;
  ~Metal3CmdEncoder() override;

  Metal3CmdEncoder(MetalDevice* device, MTL::CommandBuffer* cmd_buf);

  void begin_rendering(std::initializer_list<rhi::RenderingAttachmentInfo> attachments) override;
  void end_encoding() override;
  void bind_pipeline(rhi::PipelineHandle handle) override;
  void set_viewport(glm::uvec2 min, glm::uvec2 extent) override;
  void set_scissor(glm::uvec2 min, glm::uvec2 extent) override;
  void draw_primitives(rhi::PrimitiveTopology topology, size_t vertex_start, size_t count,
                       size_t instance_count) override;
  void push_constants(void* data, size_t size) override;
  void draw_indexed_primitives(rhi::PrimitiveTopology topology, rhi::BufferHandle index_buf,
                               size_t index_start, size_t count, size_t instance_count,
                               size_t base_vertex_idx, size_t base_instance,
                               rhi::IndexType index_type) override;
  void set_depth_stencil_state(rhi::CompareOp depth_compare_op, bool depth_write_enabled) override;
  void set_wind_order(rhi::WindOrder wind_order) override;
  void set_cull_mode(rhi::CullMode cull_mode) override;
  // TODO: will this work with vulkanisms
  void upload_texture_data(rhi::BufferHandle src_buf, size_t src_offset, size_t src_bytes_per_row,
                           rhi::TextureHandle dst_tex) override;
  void upload_texture_data(rhi::BufferHandle src_buf, size_t src_offset, size_t src_bytes_per_row,
                           rhi::TextureHandle dst_tex, glm::uvec3 src_size,
                           glm::uvec3 dst_origin) override;
  void copy_tex_to_buf(rhi::TextureHandle src_tex, size_t src_slice, size_t src_level,
                       rhi::BufferHandle dst_buf, size_t dst_offset) override;

  uint32_t prepare_indexed_indirect_draws(rhi::BufferHandle indirect_buf, size_t offset,
                                          size_t draw_cnt, rhi::BufferHandle index_buf,
                                          size_t index_buf_offset, void* push_constant_data,
                                          size_t push_constant_size) override;
  void barrier(rhi::PipelineStage src_stage, rhi::AccessFlags src_access,
               rhi::PipelineStage dst_stage, rhi::AccessFlags dst_access) override;

  void draw_indexed_indirect(rhi::BufferHandle indirect_buf, uint32_t indirect_buf_id,
                             size_t offset, size_t draw_cnt) override;
  void draw_mesh_threadgroups(glm::uvec3 thread_groups, glm::uvec3 threads_per_task_thread_group,
                              glm::uvec3 threads_per_mesh_thread_group) override;

 private:
  void init_icb_arg_encoder_and_buf();
  void flush_compute_barriers();
  void flush_render_barriers();

  enum EncoderType {
    EncoderType_Render = 1,
    EncoderType_Compute = 1 << 1,
    EncoderType_Blit = 1 << 2,
  };
  void end_encoders_of_types(EncoderType types);
  void start_compute_encoder();
  void start_blit_encoder();

 public:
  MTL::CommandBuffer* cmd_buf_{};

 private:
  MTL::ComputeCommandEncoder* compute_enc_{};
  MTL::BlitCommandEncoder* blit_enc_{};

  MTL::RenderCommandEncoder* render_enc_{};
  MetalDevice* device_{};

  rhi::BufferHandle curr_bound_index_buf_;
  size_t curr_bound_index_buf_offset_;

  rhi::BufferHandleHolder main_icb_container_buf_;
  MTL::ArgumentEncoder* main_icb_container_arg_enc_{};

  uint8_t pc_data_[168]{};
};
