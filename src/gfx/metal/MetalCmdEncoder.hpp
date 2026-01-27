#pragma once

#include <Metal/MTLCommandEncoder.hpp>
#include <Metal/MTLGPUAddress.hpp>

#include "core/Util.hpp"
#include "gfx/CmdEncoder.hpp"
#include "gfx/metal/MetalCmdEncoderICBMgr.hpp"
#include "gfx/metal/RootLayout.hpp"

class MetalDevice;

namespace MTL4 {

class CommandBuffer;
class RenderCommandEncoder;
class ComputeCommandEncoder;
class ArgumentTable;
class RenderPassDescriptor;

}  // namespace MTL4

namespace MTL {

class CommandBuffer;
class ArgumentEncoder;
class Buffer;
class RenderPassDescriptor;
class RenderPassColorAttachmentDescriptor;
class RenderPassDepthAttachmentDescriptor;
class ComputeCommandEncoder;
class RenderCommandEncoder;
class BlitCommandEncoder;

}  // namespace MTL

template <typename API>
struct EncoderState {
  API::RenderEnc render_enc{};
  API::ComputeEnc compute_enc{};
  API::BlitEnc blit_enc{};
  API::CommandBuffer cmd_buf{};
  API::ArgTable arg_table{};
};

template <typename API>
typename API::RPDesc* create_render_pass(
    MetalDevice* device, const std::vector<rhi::RenderingAttachmentInfo>& attachments);

template <typename EncoderAPI>
class MetalCmdEncoderBase : public rhi::CmdEncoder {
 public:
  friend class MetalDevice;
  MetalCmdEncoderBase() = default;
  MetalCmdEncoderBase(const MetalCmdEncoderBase&) = delete;
  MetalCmdEncoderBase(MetalCmdEncoderBase&&) = delete;
  MetalCmdEncoderBase& operator=(const MetalCmdEncoderBase&) = delete;
  MetalCmdEncoderBase& operator=(MetalCmdEncoderBase&&) = delete;

  void set_debug_name(const char* name) override;
  void begin_rendering(std::initializer_list<rhi::RenderingAttachmentInfo> attachments) override;
  void end_rendering() override;
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
  void set_cull_mode(rhi::CullMode cull_mode) override;
  void set_wind_order(rhi::WindOrder wind_order) override;
  void upload_texture_data(rhi::BufferHandle src_buf, size_t src_offset, size_t src_bytes_per_row,
                           rhi::TextureHandle dst_tex) override;
  void upload_texture_data(rhi::BufferHandle src_buf, size_t src_offset, size_t src_bytes_per_row,
                           rhi::TextureHandle dst_tex, glm::uvec3 src_size, glm::uvec3 dst_origin,
                           int mip_level) override;
  uint32_t prepare_indexed_indirect_draws(rhi::BufferHandle indirect_buf, size_t offset,
                                          size_t tot_draw_cnt, rhi::BufferHandle index_buf,
                                          size_t index_buf_offset, void* push_constant_data,
                                          size_t push_constant_size, size_t vertex_stride) override;

  void barrier(rhi::PipelineStage src_stage, rhi::AccessFlags src_access,
               rhi::PipelineStage dst_stage, rhi::AccessFlags dst_access) override;
  void barrier(rhi::BufferHandle, rhi::PipelineStage, rhi::AccessFlags, rhi::PipelineStage,
               rhi::AccessFlags) override;
  void draw_indexed_indirect(rhi::BufferHandle indirect_buf, uint32_t indirect_buf_id,
                             size_t draw_cnt, size_t offset_i) override;
  void copy_tex_to_buf(rhi::TextureHandle src_tex, size_t src_slice, size_t src_level,
                       rhi::BufferHandle dst_buf, size_t dst_offset) override;
  void draw_mesh_threadgroups(glm::uvec3 thread_groups, glm::uvec3 threads_per_task_thread_group,
                              glm::uvec3 threads_per_mesh_thread_group) override;
  void push_debug_group(const char* name) override;
  void pop_debug_group() override;
  void dispatch_compute(glm::uvec3 thread_groups, glm::uvec3 threads_per_threadgroup) override;
  void fill_buffer(rhi::BufferHandle handle, uint32_t offset_bytes, uint32_t size,
                   uint32_t value) override;
  void draw_mesh_threadgroups_indirect(rhi::BufferHandle indirect_buf, size_t indirect_buf_offset,
                                       glm::uvec3 threads_per_task_thread_group,
                                       glm::uvec3 threads_per_mesh_thread_group) override;
  void copy_buffer_to_buffer(rhi::BufferHandle src_buf, size_t src_offset,
                             rhi::BufferHandle dst_buf, size_t dst_offset, size_t size) override;
  void bind_srv(rhi::TextureHandle texture, uint32_t slot, int subresource_id) override;
  void bind_srv(rhi::BufferHandle buffer, uint32_t slot, size_t offset_bytes) override;
  void bind_uav(rhi::TextureHandle texture, uint32_t slot, int subresource_id) override;
  void bind_uav(rhi::BufferHandle buffer, uint32_t slot, size_t offset_bytes) override;
  void bind_cbv(rhi::BufferHandle buffer, uint32_t slot, size_t offset_bytes) override;

  EncoderState<EncoderAPI> encoder_state_{};
  MetalDevice* device_{};
  std::string curr_debug_name_;
  MetalCmdEncoderICBMgr cmd_icb_mgr_;

  RootLayout root_layout_{};
  DescriptorBindingTable binding_table_{};
  bool binding_table_dirty_{false};
  bool push_constant_dirty_{false};
  bool draw_id_dirty_{false};
  bool vertex_id_base_dirty_{false};
  int64_t push_debug_group_stack_size_{};

 private:
  void reset(MetalDevice* device, EncoderAPI::CommandBuffer cmd_buf);
  void flush_barriers();
  void start_compute_encoder();
  void start_blit_encoder();
  void start_blit_equivalent_encoder();
  void flush_binds();
};

struct Metal3EncoderAPI;
struct Metal4EncoderAPI;

using Metal3CmdEncoder = MetalCmdEncoderBase<Metal3EncoderAPI>;
using Metal4CmdEncoder = MetalCmdEncoderBase<Metal4EncoderAPI>;

struct MetalRenderPassAPI3 {
  using RPDesc = MTL::RenderPassDescriptor;
  using ColorDesc = MTL::RenderPassColorAttachmentDescriptor;
  using DepthDesc = MTL::RenderPassDepthAttachmentDescriptor;

  static RPDesc* alloc_desc();
  static ColorDesc* get_color_desc(RPDesc* desc, size_t index);
  static DepthDesc* alloc_depth_desc();
  static void set_depth(RPDesc* desc, DepthDesc* depth_desc);
};

struct MetalRenderPassAPI4 {
  using RPDesc = MTL4::RenderPassDescriptor;
  using ColorDesc = MTL::RenderPassColorAttachmentDescriptor;
  using DepthDesc = MTL::RenderPassDepthAttachmentDescriptor;

  static RPDesc* alloc_desc();
  static ColorDesc* get_color_desc(RPDesc* desc, size_t index);
  static DepthDesc* alloc_depth_desc();
  static void set_depth(RPDesc* desc, DepthDesc* depth_desc);
};

struct Metal3EncoderAPI {
  using RPAPI = MetalRenderPassAPI3;
  static void set_compute_args() {}
  using ComputeEnc = MTL::ComputeCommandEncoder*;
  using RenderEnc = MTL::RenderCommandEncoder*;
  using BlitEnc = MTL::BlitCommandEncoder*;
  using CommandBuffer = MTL::CommandBuffer*;
  using EncoderState = EncoderState<Metal3EncoderAPI>;
  using ArgTable = void*;

  static void end_compute_encoder(ComputeEnc& enc);
  static void end_blit_encoder(BlitEnc& enc);
  static void end_render_encoder(RenderEnc& enc);
  static void start_compute_encoder(EncoderState& state);
  static void start_render_encoder(EncoderState& state, RPAPI::RPDesc* desc);
  static void start_blit_encoder(EncoderState& state);
  static void end_all_encoders(RenderEnc& r, ComputeEnc& c, BlitEnc& b);
};

struct Metal4EncoderAPI {
  using RPAPI = MetalRenderPassAPI4;
  static void set_compute_args() {}

  using ComputeEnc = MTL4::ComputeCommandEncoder*;
  using RenderEnc = MTL4::RenderCommandEncoder*;
  using BlitEnc = void*;  // no blit encoder in M4
  using CommandBuffer = MTL4::CommandBuffer*;
  using EncoderState = EncoderState<Metal4EncoderAPI>;
  using ArgTable = MTL4::ArgumentTable*;

  static void end_compute_encoder(ComputeEnc& enc);
  static void end_render_encoder(RenderEnc& enc);
  static void start_compute_encoder(EncoderState& state);
  static void start_render_encoder(EncoderState& state, RPAPI::RPDesc* desc);

  static void start_blit_encoder(...) {}
  static void end_blit_encoder(...) {}
};
