#pragma once

#include <Metal/MTLCommandEncoder.hpp>
#include <Metal/MTLGPUAddress.hpp>
#include <QuartzCore/CAMetalDrawable.hpp>

#include "core/Config.hpp"
#include "gfx/metal/MetalCmdEncoderICBMgr.hpp"
#include "gfx/metal/MetalRootLayout.hpp"
#include "gfx/metal/MetalSemaphore.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/Pipeline.hpp"
#include "gfx/rhi/Queue.hpp"
#include "small_vector/small_vector.hpp"

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

namespace TENG_NAMESPACE {

class MetalDevice;

struct MTL3_State {
  MTL::RenderCommandEncoder* render_enc{};
  MTL::ComputeCommandEncoder* compute_enc{};
  MTL::BlitCommandEncoder* blit_enc{};
  MTL::CommandBuffer* cmd_buf{};
  MTL::ArgumentEncoder* arg_encoder{};
};

struct MTL4_State {
  MTL4::RenderCommandEncoder* render_enc{};
  MTL4::ComputeCommandEncoder* compute_enc{};
  MTL4::CommandBuffer* cmd_buf{};
  MTL4::ArgumentTable* arg_table{};
};

template <typename API>
typename API::RPDesc* create_render_pass(MetalDevice* device,
                                         const std::vector<rhi::RenderAttInfo>& attachments);

template <bool UseMTL4 = true>
class MetalCmdEncoderBase : public rhi::CmdEncoder {
 public:
  friend class MetalDevice;
  MetalCmdEncoderBase() = default;
  MetalCmdEncoderBase(const MetalCmdEncoderBase&) = delete;
  MetalCmdEncoderBase(MetalCmdEncoderBase&&) = delete;
  MetalCmdEncoderBase& operator=(const MetalCmdEncoderBase&) = delete;
  MetalCmdEncoderBase& operator=(MetalCmdEncoderBase&&) = delete;

  void set_label(const std::string& label) override;
  void set_debug_name(const char* name) override;
  void begin_rendering(std::initializer_list<rhi::RenderAttInfo> attachments) override;
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
  void barrier(rhi::TextureHandle, rhi::PipelineStage, rhi::AccessFlags, rhi::PipelineStage,
               rhi::AccessFlags) override;
  void barrier(rhi::BufferHandle buf, rhi::PipelineStage src_stage, rhi::AccessFlags src_access,
               rhi::PipelineStage dst_stage, rhi::AccessFlags dst_access, size_t, size_t) override {
    barrier(buf, src_stage, src_access, dst_stage, dst_access);
  }
  void barrier(rhi::GPUBarrier* gpu_barrier, size_t barrier_count) override;
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

  void write_timestamp(rhi::QueryPoolHandle query_pool, uint32_t query_index) override;
  void query_resolve(rhi::QueryPoolHandle query_pool, uint32_t start_query, uint32_t query_count,
                     rhi::BufferHandle dst_buffer, size_t dst_offset) override;

  MTL3_State m3;
  MTL4_State m4;
  std::vector<MetalSemaphore> waits_;
  std::vector<MetalSemaphore> signals_;
  MTL4_State& m4_state() { return m4; }
  MTL3_State& m3_state() { return m3; }
  gch::small_vector<NS::SharedPtr<CA::MetalDrawable>, 8> presents_;
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
  bool done_{false};
  rhi::QueueType queue_;
  rhi::RenderTargetInfo curr_render_target_info_;

 private:
  void pre_dispatch();
  void pre_blit();
  void reset(MetalDevice* device);
  void flush_barriers();
  void start_blit_encoder();
  void start_compute_encoder();
  void start_blit_equivalent_encoder();
  void flush_binds();
  void end_compute_encoder();
  void end_blit_encoder();
  void end_render_encoder();
  void set_buffer(uint32_t bind_point, MTL::Buffer* buffer, size_t offset, uint32_t stages);
};

using Metal3CmdEncoder = MetalCmdEncoderBase<false>;
using Metal4CmdEncoder = MetalCmdEncoderBase<true>;

}  // namespace TENG_NAMESPACE
