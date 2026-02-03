#include "VulkanCmdEncoder.hpp"

#include "core/Config.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "gfx/vulkan/VkUtil.hpp"
#include "gfx/vulkan/VulkanCommon.hpp"
#include "gfx/vulkan/VulkanDevice.hpp"
#include "gfx/vulkan/VulkanTexture.hpp"

namespace TENG_NAMESPACE {

namespace gfx::vk {

namespace {

VkImageAspectFlags convert(rhi::ImageAspect aspect) {
  VkImageAspectFlags flags{};
  if (aspect & rhi::ImageAspect_Color) {
    flags |= VK_IMAGE_ASPECT_COLOR_BIT;
  }
  if (aspect & rhi::ImageAspect_Depth) {
    flags |= VK_IMAGE_ASPECT_DEPTH_BIT;
  }
  if (aspect & rhi::ImageAspect_Stencil) {
    flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
  }
  return flags;
}

VkAttachmentLoadOp convert_load_op(rhi::LoadOp load_op) {
  switch (load_op) {
    case rhi::LoadOp::Clear:
      return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case rhi::LoadOp::Load:
      return VK_ATTACHMENT_LOAD_OP_LOAD;
    case rhi::LoadOp::DontCare:
    default:
      return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  }
}

VkAttachmentStoreOp convert_store_op(rhi::StoreOp store_op) {
  switch (store_op) {
    case rhi::StoreOp::Store:
      return VK_ATTACHMENT_STORE_OP_STORE;
    case rhi::StoreOp::DontCare:
    default:
      return VK_ATTACHMENT_STORE_OP_DONT_CARE;
  }
}

}  // namespace

void VulkanCmdEncoder::begin_rendering(
    std::initializer_list<rhi::RenderingAttachmentInfo> attachments) {
  flush_barriers();
  VkRenderingAttachmentInfo color_attachments[8]{};
  VkRenderingAttachmentInfo ds_att{};
  uint32_t color_attachment_count = 0;
  // TODO: parameterize
  glm::uvec2 extent{0, 0};
  for (const auto& att : attachments) {
    if (att.type == rhi::RenderingAttachmentInfo::Type::Color) {
      auto* vk_att = &color_attachments[color_attachment_count++];
      auto* tex = (VulkanTexture*)device_->get_tex(att.image);
      extent.x = std::max(extent.x, tex->desc().dims.x);
      extent.y = std::max(extent.y, tex->desc().dims.y);
      vk_att->sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
      vk_att->imageView = tex->default_view_;
      vk_att->imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      vk_att->loadOp = convert_load_op(att.load_op);
      vk_att->storeOp = convert_store_op(att.store_op);
      if (att.load_op == rhi::LoadOp::Clear) {
        vk_att->clearValue.color.float32[0] = att.clear_value.color[0];
        vk_att->clearValue.color.float32[1] = att.clear_value.color[1];
        vk_att->clearValue.color.float32[2] = att.clear_value.color[2];
        vk_att->clearValue.color.float32[3] = att.clear_value.color[3];
      }
    } else {
      ds_att.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
      auto* tex = (VulkanTexture*)device_->get_tex(att.image);
      ds_att.imageView = tex->default_view_;
      extent.x = std::max(extent.x, tex->desc().dims.x);
      extent.y = std::max(extent.y, tex->desc().dims.y);
      ds_att.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      ds_att.loadOp = convert_load_op(att.load_op);
      ds_att.storeOp = convert_store_op(att.store_op);
      if (att.load_op == rhi::LoadOp::Clear) {
        ds_att.clearValue.depthStencil.depth = att.clear_value.depth_stencil.depth;
        ds_att.clearValue.depthStencil.stencil = att.clear_value.depth_stencil.stencil;
      }
    }
  }

  VkRenderingInfo rendering_info{
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {.offset = {0, 0}, .extent = {extent.x, extent.y}},
      .layerCount = 1,
      .colorAttachmentCount = color_attachment_count,
      .pColorAttachments = color_attachments,
      .pDepthAttachment = ds_att.imageView ? &ds_att : nullptr,
      .pStencilAttachment = nullptr,
  };
  ASSERT(cmd());
  vkCmdBeginRenderingKHR(cmd(), &rendering_info);
}

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
                                               size_t /*base_instance*/,
                                               rhi::IndexType /*index_type*/) {}
void VulkanCmdEncoder::set_depth_stencil_state(rhi::CompareOp /*depth_compare_op*/,
                                               bool /*depth_write_enabled*/) {}
void VulkanCmdEncoder::set_wind_order(rhi::WindOrder /*wind_order*/) {}
void VulkanCmdEncoder::set_cull_mode(rhi::CullMode /*cull_mode*/) {}

void VulkanCmdEncoder::push_constants(void* data, size_t size) {
  flush_barriers();
  vkCmdPushConstants(cmd(), shared_pipeline_layout_, VK_SHADER_STAGE_ALL, 0, (uint32_t)size, data);
}

void VulkanCmdEncoder::end_encoding() { VK_CHECK(vkEndCommandBuffer(cmd())); }

void VulkanCmdEncoder::set_viewport(glm::uvec2 /*min*/, glm::uvec2 /*extent*/) {}
void VulkanCmdEncoder::upload_texture_data(rhi::BufferHandle /*src_buf*/, size_t /*src_offset*/,
                                           size_t /*src_bytes_per_row*/,
                                           rhi::TextureHandle /*dst_tex*/) {}
void VulkanCmdEncoder::copy_tex_to_buf(rhi::TextureHandle /*src_tex*/, size_t /*src_slice*/,
                                       size_t /*src_level*/, rhi::BufferHandle /*dst_buf*/,
                                       size_t /*dst_offset*/) {}

void VulkanCmdEncoder::barrier(rhi::PipelineStage /*src_stage*/, rhi::AccessFlags /*src_access*/,
                               rhi::PipelineStage /*dst_stage*/, rhi::AccessFlags /*dst_access*/) {}
void VulkanCmdEncoder::draw_indexed_indirect(rhi::BufferHandle /*indirect_buf*/,
                                             uint32_t /* indirect_buf_id */, size_t /*draw_cnt*/,
                                             size_t /*offset_i*/) {}

void VulkanCmdEncoder::end_rendering() {
  vkCmdEndRenderingKHR(cmd());
  for (auto& b : render_pass_end_img_barriers_) {
    img_barriers_.emplace_back(b);
  }
  render_pass_end_img_barriers_.clear();
  flush_barriers();

  // VkImageMemoryBarrier2 img_barriers[] = {
  //     VkImageMemoryBarrier2{
  //         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
  //         .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
  //         .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
  //         .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
  //         .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
  //         .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  //         .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
  //         .image =
  //             ((VulkanTexture*)device_->get_tex(
  //                  ((VulkanSwapchain*)submit_swapchains_.back())
  //                      ->get_texture(((VulkanSwapchain*)submit_swapchains_.back())->curr_img_idx_)))
  //                 ->image(),
  //         .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
  //                              .levelCount = 1,
  //                              .layerCount = 1},
  //     },
  // };
  // VkDependencyInfo info{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
  //                       .imageMemoryBarrierCount = ARRAY_SIZE(img_barriers),
  //                       .pImageMemoryBarriers = img_barriers};
  // vkCmdPipelineBarrier2KHR(cmd(), &info);
}

void VulkanCmdEncoder::barrier(rhi::BufferHandle buf, rhi::PipelineStage src_stage,
                               rhi::AccessFlags src_access, rhi::PipelineStage dst_stage,
                               rhi::AccessFlags dst_access) {
  barrier(buf, src_stage, src_access, dst_stage, dst_access, 0, VK_WHOLE_SIZE);
}

void VulkanCmdEncoder::barrier(rhi::BufferHandle buf, rhi::PipelineStage src_stage,
                               rhi::AccessFlags src_access, rhi::PipelineStage dst_stage,
                               rhi::AccessFlags dst_access, size_t offset, size_t size) {
  buf_barriers_.emplace_back(VkBufferMemoryBarrier2{
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
      .srcStageMask = convert(src_stage),
      .srcAccessMask = convert(src_access),
      .dstStageMask = convert(dst_stage),
      .dstAccessMask = convert(dst_access),
      .buffer = ((VulkanBuffer*)device_->get_buf(buf))->buffer(),
      .offset = offset,
      .size = size,
  });
}

void VulkanCmdEncoder::flush_barriers() {
  if (!buf_barriers_.empty() || !img_barriers_.empty()) {
    VkDependencyInfo info{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = (uint32_t)buf_barriers_.size(),
        .pBufferMemoryBarriers = buf_barriers_.data(),
        .imageMemoryBarrierCount = (uint32_t)img_barriers_.size(),
        .pImageMemoryBarriers = img_barriers_.data(),
    };
    vkCmdPipelineBarrier2KHR(cmd(), &info);
    buf_barriers_.clear();
    img_barriers_.clear();
  }
}

// namespace {
// std::pair<VkPipelineStageFlags2, VkAccessFlags2> convert_pipeline_stage_and_access(
//     rhi::ResourceState state) {
//   VkPipelineStageFlags2 stage{};
//   VkAccessFlags2 access{};
//
//   if (state & rhi::ColorWrite) {
//     stage |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
//     access |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
//   }
//   if (state & rhi::ColorRead) {
//     stage |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
//     access |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
//   }
//   if (state & rhi::DepthStencilRead) {
//     stage |=
//         VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
//         VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
//     access |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
//   }
//   if (state & rhi::DepthStencilWrite) {
//     stage |=
//         VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
//         VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
//     access |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
//   }
//   if (state & rhi::VertexRead) {
//     stage |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
//     access |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
//   }
//   if (state & rhi::IndexRead) {
//     stage |= VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
//     access |= VK_ACCESS_2_INDEX_READ_BIT;
//   }
//   if (state & rhi::IndirectRead) {
//     stage |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
//     access |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
//   }
//   if (state & rhi::ComputeRead) {
//     stage |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
//     access |= VK_ACCESS_2_SHADER_READ_BIT;
//   }
//   if (state & rhi::ComputeWrite) {
//     stage |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
//     access |= VK_ACCESS_2_SHADER_WRITE_BIT;
//   }
//   if (state & rhi::TransferRead) {
//     stage |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
//     access |= VK_ACCESS_2_TRANSFER_READ_BIT;
//   }
//   if (state & rhi::TransferWrite) {
//     stage |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
//     access |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
//   }
//   if (state & rhi::FragmentStorageRead) {
//     stage |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
//     access |= VK_ACCESS_2_SHADER_READ_BIT;
//   }
//   if (state & rhi::ComputeSample) {
//     stage |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
//     access |= VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
//   }
//   if (state & rhi::FragmentSample) {
//     stage |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
//     access |= VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
//   }
//   if (state & rhi::ShaderRead) {
//     stage |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
//     access |= VK_ACCESS_2_SHADER_READ_BIT;
//   }
//
//   return {stage, access};
// }
//
// }  // namespace

void VulkanCmdEncoder::barrier(rhi::GPUBarrier* gpu_barrier, size_t barrier_count) {
  for (size_t i = 0; i < barrier_count; i++) {
    auto& gpu_barr = gpu_barrier[i];
    if (gpu_barr.type == rhi::GPUBarrier::Type::Buffer) {
      auto& buf_barr = gpu_barr.buf;
      buf_barriers_.emplace_back(VkBufferMemoryBarrier2{
          .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
          .srcStageMask = convert(buf_barr.src_stage),
          .srcAccessMask = convert(buf_barr.src_access),
          .dstStageMask = convert(buf_barr.dst_stage),
          .dstAccessMask = convert(buf_barr.dst_access),
          .buffer = ((VulkanBuffer*)device_->get_buf(buf_barr.buffer))->buffer(),
          .offset = buf_barr.offset,
          .size = buf_barr.size,
      });
    } else {
      auto& img_barr = gpu_barr.tex;
      img_barriers_.emplace_back(VkImageMemoryBarrier2{
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
          .srcStageMask = convert(img_barr.src_stage),
          .srcAccessMask = convert(img_barr.src_access),
          .dstStageMask = convert(img_barr.dst_stage),
          .dstAccessMask = convert(img_barr.dst_access),
          .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
          .newLayout = VK_IMAGE_LAYOUT_GENERAL,
          .image = ((VulkanTexture*)device_->get_tex(img_barr.texture))->image(),
          .subresourceRange = {.aspectMask = convert(img_barr.aspect),
                               .baseMipLevel = img_barr.mip < 0 ? 0 : img_barr.mip,
                               .levelCount = img_barr.mip < 0 ? VK_REMAINING_MIP_LEVELS : 1,
                               .baseArrayLayer = img_barr.slice < 0 ? 0 : img_barr.slice,
                               .layerCount = img_barr.slice < 0 ? VK_REMAINING_ARRAY_LAYERS : 1},
      });
    }
  }
}

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
