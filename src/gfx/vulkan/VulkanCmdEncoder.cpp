#include "VulkanCmdEncoder.hpp"

#include "gfx/vulkan/VulkanCommon.hpp"

namespace gfx::vk {
void VulkanCmdEncoder::begin_rendering(
    std::initializer_list<rhi::RenderingAttachmentInfo> /*attachments*/) {}
void VulkanCmdEncoder::bind_pipeline(rhi::PipelineHandle /*handle*/) {}
void VulkanCmdEncoder::bind_pipeline(const rhi::PipelineHandleHolder& handle) {
  bind_pipeline(handle.handle);
}
void VulkanCmdEncoder::draw_primitives(rhi::PrimitiveTopology /*topology*/, size_t /*vertex_start*/,
                                       size_t /*count*/, size_t /*instance_count*/) {}
void VulkanCmdEncoder::draw_indexed_primitives(rhi::PrimitiveTopology /*topology*/,
                                               rhi::BufferHandle /*index_buf*/,
                                               size_t /*index_start*/, size_t /*count*/,
                                               size_t /*instance_count*/, size_t /*base_vertex*/,
                                               size_t /*base_instance*/) {}
void VulkanCmdEncoder::set_depth_stencil_state(rhi::CompareOp /*depth_compare_op*/,
                                               bool /*depth_write_enabled*/) {}
void VulkanCmdEncoder::set_wind_order(rhi::WindOrder /*wind_order*/) {}
void VulkanCmdEncoder::set_cull_mode(rhi::CullMode /*cull_mode*/) {}

void VulkanCmdEncoder::push_constants(void* data, size_t size) {
  vkCmdPushConstants(cmd_buf_, shared_pipeline_layout_, VK_SHADER_STAGE_ALL, 0, size, data);
}

void VulkanCmdEncoder::end_encoding() { VK_CHECK(vkEndCommandBuffer(cmd_buf_)); }

void VulkanCmdEncoder::set_viewport(glm::uvec2 /*min*/, glm::uvec2 /*extent*/) {}
void VulkanCmdEncoder::upload_texture_data(rhi::BufferHandle /*src_buf*/, size_t /*src_offset*/,
                                           size_t /*src_bytes_per_row*/,
                                           rhi::TextureHandle /*dst_tex*/) {}
void VulkanCmdEncoder::copy_tex_to_buf(rhi::TextureHandle /*src_tex*/, size_t /*src_slice*/,
                                       size_t /*src_level*/, rhi::BufferHandle /*dst_buf*/,
                                       size_t /*dst_offset*/) {}
uint32_t VulkanCmdEncoder::prepare_indexed_indirect_draws(rhi::BufferHandle /* indirect_buf */,
                                                          size_t /*offset*/, size_t /*draw_cnt*/,
                                                          rhi::BufferHandle /*index_buf*/,
                                                          size_t /*index_buf_offset*/,
                                                          void* /*push_constant_data*/,
                                                          size_t /*push_constant_size*/) {
  exit(1);
}
void VulkanCmdEncoder::barrier(rhi::PipelineStage /*src_stage*/, rhi::AccessFlags /*src_access*/,
                               rhi::PipelineStage /*dst_stage*/, rhi::AccessFlags /*dst_access*/) {}
void VulkanCmdEncoder::draw_indexed_indirect(rhi::BufferHandle /*indirect_buf*/,
                                             uint32_t /* indirect_buf_id */, size_t /*offset*/,
                                             size_t /*draw_cnt*/) {}
}  // namespace gfx::vk
