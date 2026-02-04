#pragma once

#include <vulkan/vulkan_core.h>

#include "core/Config.hpp"
#include "gfx/metal/RootLayout.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/Config.hpp"
#include "gfx/rhi/Pipeline.hpp"
#include "gfx/rhi/Queue.hpp"

namespace TENG_NAMESPACE {

namespace gfx::vk {
class VulkanDevice;
class VulkanPipeline;

struct DescriptorBinderPool {
  VkDescriptorPool pool{};
  uint32_t pool_size{256};
  void init(VulkanDevice& device);
  void reset(VulkanDevice& device) const;
  void destroy(VulkanDevice& device);
};

struct DescriptorBinder {
  std::vector<VkWriteDescriptorSet> writes;
  std::vector<VkDescriptorImageInfo> img_infos;
};

class VulkanCmdEncoder : public rhi::CmdEncoder {
 public:
  explicit VulkanCmdEncoder(VulkanDevice* device);

  void set_debug_name(const char* /*name*/) override { exit(1); }
  void begin_rendering(std::initializer_list<rhi::RenderingAttachmentInfo> attachments) override;
  void end_rendering() override;

  void bind_pipeline(rhi::PipelineHandle handle) override;
  void bind_pipeline(const rhi::PipelineHandleHolder& handle);

  void draw_primitives(rhi::PrimitiveTopology topology, size_t vertex_start, size_t count,
                       size_t instance_count) override;
  void draw_primitives(rhi::PrimitiveTopology topology, size_t vertex_start, size_t count) {
    draw_primitives(topology, vertex_start, count, 1);
  }
  void draw_primitives(rhi::PrimitiveTopology topology, size_t count) {
    draw_primitives(topology, 0, count, 1);
  }

  void draw_indexed_primitives(rhi::PrimitiveTopology topology, rhi::BufferHandle index_buf,
                               size_t index_start, size_t count, size_t instance_count,
                               size_t base_vertex, size_t base_instance,
                               rhi::IndexType index_type) override;
  void set_depth_stencil_state(rhi::CompareOp depth_compare_op, bool depth_write_enabled) override;
  void set_wind_order(rhi::WindOrder wind_order) override;
  void set_cull_mode(rhi::CullMode cull_mode) override;

  void push_constants(void* data, size_t size) override;
  void end_encoding() override;
  void set_label(const std::string& /*label*/) override { exit(1); }
  void set_viewport(glm::uvec2 min, glm::uvec2 extent) override;
  void set_scissor(glm::uvec2 min, glm::uvec2 extent) override;

  void upload_texture_data(rhi::BufferHandle src_buf, size_t src_offset, size_t src_bytes_per_row,
                           rhi::TextureHandle dst_tex) override;
  void upload_texture_data(rhi::BufferHandle /*src_buf*/, size_t /*src_offset*/,
                           size_t /*src_bytes_per_row*/, rhi::TextureHandle /*dst_tex*/,
                           glm::uvec3 /*src_size*/, glm::uvec3 /*dst_origin*/,
                           int /*mip_level*/) override {
    exit(1);
  }

  void copy_tex_to_buf(rhi::TextureHandle src_tex, size_t src_slice, size_t src_level,
                       rhi::BufferHandle dst_buf, size_t dst_offset) override;
  void copy_buffer_to_buffer(rhi::BufferHandle /*src_buf*/, size_t /*src_offset*/,
                             rhi::BufferHandle /*dst_buf*/, size_t /*dst_offset*/,
                             size_t /*size*/) override {
    exit(1);
  }

  [[nodiscard]] uint32_t prepare_indexed_indirect_draws(
      rhi::BufferHandle /*indirect_buf*/, size_t /*offset*/, size_t /*tot_draw_cnt*/,
      rhi::BufferHandle /*index_buf*/, size_t /*index_buf_offset*/, void* /*push_constant_data*/,
      size_t /*push_constant_size*/, size_t /*vertex_stride*/) override {
    exit(1);
  }

  void barrier(rhi::PipelineStage src_stage, rhi::AccessFlags src_access,
               rhi::PipelineStage dst_stage, rhi::AccessFlags dst_access) override;
  void barrier(rhi::BufferHandle buf, rhi::PipelineStage src_stage, rhi::AccessFlags src_access,
               rhi::PipelineStage dst_stage, rhi::AccessFlags dst_access) override;
  void barrier(rhi::BufferHandle buf, rhi::PipelineStage src_stage, rhi::AccessFlags src_access,
               rhi::PipelineStage dst_stage, rhi::AccessFlags dst_access, size_t offset,
               size_t size) override;

  void barrier(rhi::GPUBarrier* gpu_barrier, size_t barrier_count) override;

  void draw_indexed_indirect(rhi::BufferHandle indirect_buf, uint32_t indirect_buf_id,
                             size_t draw_cnt, size_t offset_i) override;

  void draw_mesh_threadgroups(glm::uvec3 /*thread_groups*/,
                              glm::uvec3 /*threads_per_task_thread_group*/,
                              glm::uvec3 /*threads_per_mesh_thread_group*/) override {
    exit(1);
  }
  void draw_mesh_threadgroups_indirect(rhi::BufferHandle /*indirect_buf*/,
                                       size_t /*indirect_buf_offset*/,
                                       glm::uvec3 /*threads_per_task_thread_group*/,
                                       glm::uvec3 /*threads_per_mesh_thread_group*/) override {
    exit(1);
  }

  void dispatch_compute(glm::uvec3 thread_groups, glm::uvec3 threads_per_threadgroup) override;

  void fill_buffer(rhi::BufferHandle /*handle*/, uint32_t /*offset_bytes*/, uint32_t /*size*/,
                   uint32_t /*value*/) override {
    exit(1);
  }
  void push_debug_group(const char* /*name*/) override { exit(1); }
  void pop_debug_group() override { exit(1); }

  void bind_srv(rhi::TextureHandle /*texture*/, uint32_t /*slot*/,
                int /*subresource_id*/) override {
    exit(1);
  }
  void bind_srv(rhi::BufferHandle /*buffer*/, uint32_t /*slot*/, size_t /*offset_bytes*/) override {
    exit(1);
  }

  void bind_uav(rhi::TextureHandle texture, uint32_t slot, int subresource_id) override;
  void bind_uav(rhi::BufferHandle /*buffer*/, uint32_t /*slot*/, size_t /*offset_bytes*/) override {
    exit(1);
  }

  void bind_cbv(rhi::BufferHandle /*buffer*/, uint32_t /*slot*/, size_t /*offset_bytes*/) override {
    exit(1);
  }

  void write_timestamp(rhi::QueryPoolHandle /*query_pool*/, uint32_t /*query_index*/) override {
    exit(1);
  }
  void query_resolve(rhi::QueryPoolHandle /*query_pool*/, uint32_t /*start_query*/,
                     uint32_t /*query_count*/, rhi::BufferHandle /*dst_buffer*/,
                     size_t /*dst_offset*/) override {
    exit(1);
  }

 private:
  friend class VulkanDevice;
  VkCommandBuffer cmd() { return cmd_bufs_[curr_frame_i_]; }
  void flush_barriers();
  void flush_binds();
  [[nodiscard]] VkPipelineBindPoint get_bound_pipeline_bind_point() const;

  size_t curr_frame_i_{};
  VkCommandBuffer cmd_bufs_[k_max_frames_in_flight];
  VulkanPipeline* bound_pipeline_{};
  VulkanDevice* device_{};
  VkDevice vk_device_{};
  std::vector<rhi::Swapchain*> submit_swapchains_;
  std::vector<VkBufferMemoryBarrier2> buf_barriers_;
  std::vector<VkImageMemoryBarrier2> img_barriers_;
  DescriptorBinderPool binder_pools_[k_max_frames_in_flight];
  DescriptorBinder binder_;

  // only valid during render pass
  rhi::RenderTargetInfo curr_render_target_info_;

  DescriptorBindingTable binding_table_{};
  bool descriptors_dirty_{false};

  // initial use is for swapchain rendering
  std::vector<VkImageMemoryBarrier2> render_pass_end_img_barriers_;

  rhi::QueueType queue_type_{};
};

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
