#include "VulkanCmdEncoder.hpp"

#include <volk.h>

#include "core/Config.hpp"
#include "core/Hash.hpp"
#include "core/Logger.hpp"
#include "core/Util.hpp"
#include "gfx/rhi/Barrier.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "gfx/vulkan/VkUtil.hpp"
#include "gfx/vulkan/VulkanBuffer.hpp"
#include "gfx/vulkan/VulkanCommon.hpp"
#include "gfx/vulkan/VulkanDevice.hpp"
#include "gfx/vulkan/VulkanTexture.hpp"
#include "vulkan/vk_enum_string_helper.h"
#include "vulkan/vulkan_core.h"

namespace TENG_NAMESPACE {

namespace gfx::vk {

namespace {

VkDependencyInfoKHR make_dependency_info(VkImageMemoryBarrier2* img_barrier,
                                         uint32_t barrier_count = 1) {
  return VkDependencyInfoKHR{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,
      .dependencyFlags = 0,
      .memoryBarrierCount = 0,
      .pMemoryBarriers = nullptr,
      .bufferMemoryBarrierCount = 0,
      .pBufferMemoryBarriers = nullptr,
      .imageMemoryBarrierCount = barrier_count,
      .pImageMemoryBarriers = img_barrier,
  };
}

std::pair<VkPipelineStageFlags2, VkAccessFlags2> convert_pipeline_stage_and_access(
    rhi::ResourceState state) {
  VkPipelineStageFlags2 stage{};
  VkAccessFlags2 access{};

  if (has_flag(state, rhi::ResourceState::ColorWrite)) {
    stage |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    access |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  }
  if (has_flag(state, rhi::ResourceState::ColorRead)) {
    stage |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    access |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
  }
  if (has_flag(state, rhi::ResourceState::DepthStencilRead)) {
    stage |=
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    access |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
  }
  if (has_flag(state, rhi::ResourceState::DepthStencilWrite)) {
    stage |=
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    access |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  }
  if (has_flag(state, rhi::ResourceState::VertexRead)) {
    stage |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    access |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
  }
  if (has_flag(state, rhi::ResourceState::IndexRead)) {
    stage |= VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
    access |= VK_ACCESS_2_INDEX_READ_BIT;
  }
  if (has_flag(state, rhi::ResourceState::IndirectRead)) {
    stage |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    access |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
  }
  if (has_flag(state, rhi::ResourceState::ComputeRead)) {
    stage |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    access |= VK_ACCESS_2_SHADER_READ_BIT;
  }
  if (has_flag(state, rhi::ResourceState::ComputeWrite)) {
    stage |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    access |= VK_ACCESS_2_SHADER_WRITE_BIT;
  }
  if (has_flag(state, rhi::ResourceState::TransferRead)) {
    stage |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    access |= VK_ACCESS_2_TRANSFER_READ_BIT;
  }
  if (has_flag(state, rhi::ResourceState::TransferWrite)) {
    stage |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    access |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
  }
  if (has_flag(state, rhi::ResourceState::FragmentStorageRead)) {
    stage |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    access |= VK_ACCESS_2_SHADER_READ_BIT;
  }
  if (has_flag(state, rhi::ResourceState::ComputeSample)) {
    stage |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    access |= VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
  }
  if (has_flag(state, rhi::ResourceState::FragmentSample)) {
    stage |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    access |= VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
  }
  if (has_flag(state, rhi::ResourceState::ShaderRead)) {
    stage |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    access |= VK_ACCESS_2_SHADER_READ_BIT;
  }
  if (has_flag(state, rhi::ResourceState::SwapchainPresent)) {
    stage |= VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    access |= 0;
  }

  return {stage, access};
}

VkImageLayout convert_layout(rhi::ResourceState state) {
  if (has_flag(state, rhi::ResourceState::ColorWrite) ||
      has_flag(state, rhi::ResourceState::ColorRead)) {
    return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  }
  if (has_flag(state, rhi::ResourceState::DepthStencilWrite)) {
    return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  }
  if (has_flag(state, rhi::ResourceState::DepthStencilRead)) {
    return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
  }
  if (has_flag(state, rhi::ResourceState::ShaderRead)) {
    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  }
  if (has_flag(state, rhi::ResourceState::ComputeWrite) ||
      has_flag(state, rhi::ResourceState::ComputeRead)) {
    return VK_IMAGE_LAYOUT_GENERAL;
  }
  if (has_flag(state, rhi::ResourceState::TransferRead)) {
    return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  }
  if (has_flag(state, rhi::ResourceState::TransferWrite)) {
    return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  }
  if (has_flag(state, rhi::ResourceState::SwapchainPresent)) {
    return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  }
  return VK_IMAGE_LAYOUT_UNDEFINED;
}

VkImageLayout layout_from_vk_access(VkAccessFlags2 access) {
  if (access & VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT) {
    return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  }
  if (access & VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT) {
    return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  }
  if (access & VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) {
    return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  }
  if (access & VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT) {
    return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
  }
  if (access & (VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT)) {
    return VK_IMAGE_LAYOUT_GENERAL;
  }
  if (access & VK_ACCESS_2_SHADER_READ_BIT) {
    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  }
  if (access & VK_ACCESS_2_SHADER_STORAGE_READ_BIT) {
    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  }
  if (access & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) {
    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  }
  if (access & VK_ACCESS_2_TRANSFER_READ_BIT) {
    return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  }
  if (access & VK_ACCESS_2_TRANSFER_WRITE_BIT) {
    return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  }
  return VK_IMAGE_LAYOUT_UNDEFINED;
}

[[nodiscard]] uint32_t uncompressed_texel_bytes(rhi::TextureFormat fmt) {
  using enum rhi::TextureFormat;
  switch (fmt) {
    case R8G8B8A8Srgb:
    case R8G8B8A8Unorm:
    case B8G8R8A8Unorm:
    case B8G8R8A8Srgb:
    case R32float:
    case D32float:
      return 4;
    case R16G16B16A16Sfloat:
      return 8;
    case R32G32B32A32Sfloat:
      return 16;
    case Undefined:
    case ASTC4x4UnormBlock:
    case ASTC4x4SrgbBlock:
    default:
      return 0;
  }
}

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

VulkanCmdEncoder::VulkanCmdEncoder(VulkanDevice* device)
    : device_(device), vk_device_(device_->vk_device()) {}

void VulkanCmdEncoder::begin_rendering(std::initializer_list<rhi::RenderAttInfo> attachments) {
  flush_barriers();
  VkRenderingAttachmentInfo color_attachments[8]{};
  VkRenderingAttachmentInfo ds_att{};
  uint32_t color_attachment_count = 0;
  // TODO: parameterize
  glm::uvec2 extent{0, 0};
  curr_render_target_info_ = {};
  for (const auto& att : attachments) {
    if (att.type == rhi::RenderAttInfo::Type::Color) {
      auto* vk_att = &color_attachments[color_attachment_count++];
      auto* tex = (VulkanTexture*)device_->get_tex(att.image);
      curr_render_target_info_.color_formats.push_back(tex->desc().format);
      extent.x = std::max(extent.x, tex->desc().dims.x);
      extent.y = std::max(extent.y, tex->desc().dims.y);
      vk_att->sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
      vk_att->imageView = device_->get_vk_tex_view(att.image, att.subresource);
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
      ds_att.imageView = device_->get_vk_tex_view(att.image, att.subresource);
      curr_render_target_info_.depth_format = tex->desc().format;
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

void VulkanCmdEncoder::bind_pipeline(rhi::PipelineHandle handle) {
  auto* pipeline = (VulkanPipeline*)device_->get_pipeline(handle);
  VkPipelineBindPoint bindpoint = pipeline->type() == rhi::PipelineType::Graphics
                                      ? VK_PIPELINE_BIND_POINT_GRAPHICS
                                      : VK_PIPELINE_BIND_POINT_COMPUTE;
  if (bindpoint == VK_PIPELINE_BIND_POINT_GRAPHICS) {
    auto render_target_info_hash = compute_render_target_info_hash(curr_render_target_info_);
    if (render_target_info_hash != pipeline->render_target_info_hash_) {
      auto new_desc = pipeline->gfx_desc();
      new_desc.rendering = curr_render_target_info_;
      auto h = std::make_tuple(render_target_info_hash, handle.to64());
      auto hash = util::hash::tuple_hash<decltype(h)>()(h);
      auto it = device_->all_pipelines_.find(hash);
      if (it == device_->all_pipelines_.end()) {
        auto new_pipeline = device_->create_graphics_pipeline(new_desc);
        device_->all_pipelines_[hash] = new_pipeline;
        pipeline = reinterpret_cast<VulkanPipeline*>(device_->get_pipeline(new_pipeline));
      } else {
        pipeline = reinterpret_cast<VulkanPipeline*>(device_->get_pipeline(it->second));
      }
    }
  }

  descriptors_dirty_ = true;
  bound_pipeline_ = pipeline;
  ASSERT(pipeline);
  vkCmdBindPipeline(cmd(), bindpoint, pipeline->pipeline_);
  if (!pipeline->bindless_descriptor_sets_.empty()) {
    vkCmdBindDescriptorSets(cmd(), bindpoint, pipeline->layout_, pipeline->bindless_first_set_,
                            static_cast<uint32_t>(pipeline->bindless_descriptor_sets_.size()),
                            pipeline->bindless_descriptor_sets_.data(), 0, nullptr);
  }
}

void VulkanCmdEncoder::bind_pipeline(const rhi::PipelineHandleHolder& handle) {
  bind_pipeline(handle.handle);
}

void VulkanCmdEncoder::draw_primitives(rhi::PrimitiveTopology topologyg, size_t vertex_start,
                                       size_t count, size_t instance_count) {
  flush_binds();
  // TODO: SOOOOOOOOOOOO BADDDDDDDDDDDDDDDDDD
  vkCmdSetPrimitiveTopologyEXT(cmd(), convert_prim_topology(topologyg));
  vkCmdDraw(cmd(), static_cast<uint32_t>(count), static_cast<uint32_t>(instance_count),
            // TODO: is this bytes or index
            static_cast<uint32_t>(vertex_start), 0);
}

void VulkanCmdEncoder::draw_indexed_primitives(rhi::PrimitiveTopology topology,
                                               rhi::BufferHandle index_buf, size_t index_start,
                                               size_t count, size_t instance_count,
                                               size_t base_vertex, size_t base_instance,
                                               rhi::IndexType index_type) {
  flush_binds();
  auto* ib = static_cast<VulkanBuffer*>(device_->get_buf(index_buf));
  ASSERT(ib);
  const VkIndexType vk_index_type =
      index_type == rhi::IndexType::Uint16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
  vkCmdBindIndexBuffer(cmd(), ib->buffer(), static_cast<VkDeviceSize>(index_start), vk_index_type);
  vkCmdSetPrimitiveTopologyEXT(cmd(), convert_prim_topology(topology));
  vkCmdDrawIndexed(cmd(), static_cast<uint32_t>(count), static_cast<uint32_t>(instance_count), 0,
                   static_cast<int32_t>(base_vertex), static_cast<uint32_t>(base_instance));
}

void VulkanCmdEncoder::set_depth_stencil_state(rhi::CompareOp /*depth_compare_op*/,
                                               bool /*depth_write_enabled*/) {}

void VulkanCmdEncoder::set_wind_order(rhi::WindOrder wind_order) {
  vkCmdSetFrontFaceEXT(cmd(), wind_order == rhi::WindOrder::Clockwise
                                  ? VK_FRONT_FACE_CLOCKWISE
                                  : VK_FRONT_FACE_COUNTER_CLOCKWISE);
}

void VulkanCmdEncoder::set_cull_mode(rhi::CullMode cull_mode) {
  vkCmdSetCullModeEXT(cmd(), convert(cull_mode));
}

void VulkanCmdEncoder::push_constants(void* data, size_t size) {
  ASSERT(bound_pipeline_);
  ASSERT(bound_pipeline_->layout_);
  ASSERT(bound_pipeline_->push_constant_stages_ != 0);
  vkCmdPushConstants(cmd(), bound_pipeline_->layout_, bound_pipeline_->push_constant_stages_, 0,
                     (uint32_t)size, data);
}

void VulkanCmdEncoder::end_encoding() {
  flush_barriers();
  VK_CHECK(vkEndCommandBuffer(cmd()));
}

void VulkanCmdEncoder::set_viewport(glm::uvec2 min, glm::uvec2 extent) {
  VkViewport viewport{
      .x = static_cast<float>(min.x),
      .y = static_cast<float>(min.y),
      .width = static_cast<float>(extent.x),
      .height = static_cast<float>(extent.y),
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
  };
  vkCmdSetViewport(cmd(), 0, 1, &viewport);
  // TODO: move this to set_scissor
  VkRect2D scissor{
      .offset = {0, 0},
      .extent = {extent.x, extent.y},
  };
  vkCmdSetScissor(cmd(), 0, 1, &scissor);
}

void VulkanCmdEncoder::upload_texture_data(rhi::BufferHandle src_buf, size_t src_offset,
                                           size_t src_bytes_per_row, rhi::TextureHandle dst_tex) {
  upload_texture_data(src_buf, src_offset, src_bytes_per_row, dst_tex, glm::uvec3{0, 0, 0},
                      glm::uvec3{0, 0, 0}, -1);
}

void VulkanCmdEncoder::upload_texture_data(rhi::BufferHandle src_buf, size_t src_offset,
                                           size_t src_bytes_per_row, rhi::TextureHandle dst_tex,
                                           glm::uvec3 src_size, glm::uvec3 dst_origin,
                                           int mip_level) {
  auto* tex = (VulkanTexture*)device_->get_tex(dst_tex);
  ASSERT(tex);
  auto* buf = static_cast<VulkanBuffer*>(device_->get_buf(src_buf));
  ASSERT(buf);
  if (src_size.x == 0 && src_size.y == 0 && src_size.z == 0) {
    src_size = {tex->desc().dims.x, tex->desc().dims.y, std::max(1u, tex->desc().dims.z)};
  }
  const uint32_t bpp = uncompressed_texel_bytes(tex->desc().format);
  ASSERT(bpp > 0);
  ASSERT(src_bytes_per_row % bpp == 0);
  const auto row_texels = static_cast<uint32_t>(src_bytes_per_row / bpp);
  ASSERT(row_texels >= static_cast<uint32_t>(src_size.x));
  VkBufferImageCopy region{
      .bufferOffset = static_cast<VkDeviceSize>(src_offset),
      .bufferRowLength = (row_texels == static_cast<uint32_t>(src_size.x)) ? 0u : row_texels,
      .bufferImageHeight = 0,
  };
  region.imageSubresource.mipLevel = mip_level < 0 ? 0 : static_cast<uint32_t>(mip_level);
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  // TODO: don't hard code this
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageOffset.x = static_cast<int32_t>(dst_origin.x);
  region.imageOffset.y = static_cast<int32_t>(dst_origin.y);
  region.imageOffset.z = static_cast<int32_t>(dst_origin.z);
  region.imageExtent.width = static_cast<uint32_t>(src_size.x);
  region.imageExtent.height = static_cast<uint32_t>(src_size.y);
  region.imageExtent.depth = static_cast<uint32_t>(src_size.z);
  VkImageLayout pre_final_barrier_layout{};
  {
    VkImageMemoryBarrier2 img_barrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    img_barrier.image = tex->image();
    img_barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    img_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    img_barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
    img_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    img_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    img_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    pre_final_barrier_layout = img_barrier.newLayout;
    img_barrier.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                    .baseMipLevel = 0,
                                    .levelCount = tex->desc().mip_levels,
                                    .baseArrayLayer = 0,
                                    .layerCount = tex->desc().array_length};
    VkDependencyInfoKHR dep_info = make_dependency_info(&img_barrier);
    vkCmdPipelineBarrier2(cmd(), &dep_info);
  }

  vkCmdCopyBufferToImage(cmd(), buf->buffer(), tex->image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         1, &region);

  if (mip_level < 0 && tex->desc().mip_levels > 1) {
    uint32_t array_layers = tex->desc().array_length;
    uint32_t mip_levels = tex->desc().mip_levels;
    VkExtent2D curr_img_size = {tex->desc().dims.x, tex->desc().dims.y};
    VkImageLayout curr_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    for (uint32_t mip_level = 0; mip_level < mip_levels; mip_level++) {
      VkImageMemoryBarrier2 img_barrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
      img_barrier.image = tex->image();
      img_barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
      img_barrier.srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
      img_barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
      img_barrier.dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
      img_barrier.oldLayout = curr_layout;
      curr_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      img_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      img_barrier.subresourceRange =
          VkImageSubresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                  .baseMipLevel = mip_level,
                                  .levelCount = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount = array_layers};
      VkDependencyInfoKHR dep_info = make_dependency_info(&img_barrier);
      vkCmdPipelineBarrier2KHR(cmd(), &dep_info);
      if (mip_level < mip_levels - 1) {
        VkExtent2D half_img_size = {curr_img_size.width / 2, curr_img_size.height / 2};
        VkImageBlit2KHR blit{
            .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2_KHR,
            .srcSubresource =
                VkImageSubresourceLayers{
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = mip_level,
                    .baseArrayLayer = 0,
                    .layerCount = array_layers,
                },
            .srcOffsets = {VkOffset3D{},
                           VkOffset3D{static_cast<int32_t>(curr_img_size.width),
                                      static_cast<int32_t>(curr_img_size.height), 1u}},
            .dstSubresource = VkImageSubresourceLayers{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                       .mipLevel = mip_level + 1,
                                                       .baseArrayLayer = 0,
                                                       .layerCount = array_layers},
            .dstOffsets = {VkOffset3D{},
                           VkOffset3D{static_cast<int32_t>(half_img_size.width),
                                      static_cast<int32_t>(half_img_size.height), 1u}}};
        VkBlitImageInfo2KHR info{.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2_KHR,
                                 .srcImage = tex->image(),
                                 .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                 .dstImage = tex->image(),
                                 .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 .regionCount = 1,
                                 .pRegions = &blit,
                                 .filter = VK_FILTER_LINEAR};
        vkCmdBlitImage2(cmd(), &info);
        curr_img_size = half_img_size;
      }
    }
    pre_final_barrier_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  }

  {
    VkImageMemoryBarrier2 img_barrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    img_barrier.image = tex->image();
    img_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    img_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    img_barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
    img_barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    img_barrier.oldLayout = pre_final_barrier_layout;
    img_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    img_barrier.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                    .baseMipLevel = 0,
                                    .levelCount = tex->desc().mip_levels,
                                    .baseArrayLayer = 0,
                                    .layerCount = tex->desc().array_length};
    VkDependencyInfoKHR dep_info = make_dependency_info(&img_barrier);
    vkCmdPipelineBarrier2(cmd(), &dep_info);
  }
}

void VulkanCmdEncoder::copy_tex_to_buf(rhi::TextureHandle /*src_tex*/, size_t /*src_slice*/,
                                       size_t /*src_level*/, rhi::BufferHandle /*dst_buf*/,
                                       size_t /*dst_offset*/) {}

void VulkanCmdEncoder::copy_buffer_to_buffer(rhi::BufferHandle src_buf, size_t src_offset,
                                             rhi::BufferHandle dst_buf, size_t dst_offset,
                                             size_t size) {
  if (size == 0) {
    return;
  }
  flush_barriers();
  auto* src_b = static_cast<VulkanBuffer*>(device_->get_buf(src_buf));
  auto* dst_b = static_cast<VulkanBuffer*>(device_->get_buf(dst_buf));
  const VkBufferCopy region{
      .srcOffset = static_cast<VkDeviceSize>(src_offset),
      .dstOffset = static_cast<VkDeviceSize>(dst_offset),
      .size = static_cast<VkDeviceSize>(size),
  };
  vkCmdCopyBuffer(cmd(), src_b->buffer(), dst_b->buffer(), 1, &region);
}

void VulkanCmdEncoder::barrier(rhi::PipelineStage /*src_stage*/, rhi::AccessFlags /*src_access*/,
                               rhi::PipelineStage /*dst_stage*/, rhi::AccessFlags /*dst_access*/) {
  ASSERT(0 && "TODO");
}

uint32_t VulkanCmdEncoder::prepare_indexed_indirect_draws(
    [[maybe_unused]] rhi::BufferHandle indirect_buf, [[maybe_unused]] size_t offset,
    [[maybe_unused]] size_t tot_draw_cnt, [[maybe_unused]] rhi::BufferHandle index_buf,
    [[maybe_unused]] size_t index_buf_offset, void* push_constant_data, size_t push_constant_size,
    [[maybe_unused]] size_t vertex_stride) {
  ASSERT(push_constant_size <= 256u);
  if (push_constant_size > 0) {
    ASSERT(push_constant_data);
  }
  auto& slot = device_->indexed_indirect_pc_cache_[curr_frame_i_];
  slot.slots.emplace_back(VulkanDevice::IndexedIndirectPCSlots::Slot{
      .pc = std::vector<uint8_t>(push_constant_size),
      .index_buf = index_buf,
      .index_buf_offset = index_buf_offset,
  });
  if (push_constant_size > 0) {
    std::memcpy(slot.slots.back().pc.data(), push_constant_data, push_constant_size);
  }
  return static_cast<uint32_t>(slot.slots.size() - 1);
}

void VulkanCmdEncoder::draw_indexed_indirect(rhi::BufferHandle indirect_buf,
                                             uint32_t indirect_buf_id, size_t draw_cnt,
                                             size_t offset_i) {
  flush_binds();
  auto& slot = device_->indexed_indirect_pc_cache_[curr_frame_i_];
  ASSERT(indirect_buf_id < slot.slots.size());
  const std::vector<uint8_t>& pc = slot.slots[indirect_buf_id].pc;
  if (!pc.empty()) {
    ASSERT(bound_pipeline_);
    ASSERT(bound_pipeline_->layout_);
    ASSERT(bound_pipeline_->push_constant_stages_ != 0);
    vkCmdPushConstants(cmd(), bound_pipeline_->layout_, bound_pipeline_->push_constant_stages_, 0,
                       static_cast<uint32_t>(pc.size()), pc.data());
  }
  auto* buf = static_cast<VulkanBuffer*>(device_->get_buf(indirect_buf));
  ASSERT(buf);
  auto* index_buf =
      static_cast<VulkanBuffer*>(device_->get_buf(slot.slots[indirect_buf_id].index_buf));
  ASSERT(index_buf);
  vkCmdBindIndexBuffer(cmd(), index_buf->buffer(), slot.slots[indirect_buf_id].index_buf_offset,
                       // TODO: don't hard code u32
                       VK_INDEX_TYPE_UINT32);
  vkCmdSetPrimitiveTopologyEXT(cmd(), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  vkCmdDrawIndexedIndirect(cmd(), buf->buffer(), offset_i, draw_cnt,
                           sizeof(VkDrawIndexedIndirectCommand));
}

void VulkanCmdEncoder::draw_mesh_threadgroups(glm::uvec3 thread_groups,
                                              glm::uvec3 /*threads_per_task_thread_group*/,
                                              glm::uvec3 /*threads_per_mesh_thread_group*/) {
  flush_binds();
  vkCmdDrawMeshTasksEXT(cmd(), thread_groups.x, thread_groups.y, thread_groups.z);
}

void VulkanCmdEncoder::draw_mesh_threadgroups_indirect(
    rhi::BufferHandle indirect_buf, size_t indirect_buf_offset,
    [[maybe_unused]] glm::uvec3 threads_per_task_thread_group,
    [[maybe_unused]] glm::uvec3 threads_per_mesh_thread_group) {
  flush_binds();
  auto* buf = static_cast<VulkanBuffer*>(device_->get_buf(indirect_buf));
  ASSERT(buf);
  vkCmdDrawMeshTasksIndirectEXT(cmd(), buf->buffer(), indirect_buf_offset, 1,
                                sizeof(VkDrawMeshTasksIndirectCommandEXT));
}

void VulkanCmdEncoder::end_rendering() {
  vkCmdEndRenderingKHR(cmd());
  for (auto& b : render_pass_end_img_barriers_) {
    img_barriers_.emplace_back(b);
  }
  render_pass_end_img_barriers_.clear();
  flush_barriers();
}

void VulkanCmdEncoder::barrier(rhi::BufferHandle buf, rhi::PipelineStage src_stage,
                               rhi::AccessFlags src_access, rhi::PipelineStage dst_stage,
                               rhi::AccessFlags dst_access) {
  barrier(buf, src_stage, src_access, dst_stage, dst_access, 0, VK_WHOLE_SIZE);
}

void VulkanCmdEncoder::barrier(rhi::BufferHandle buf, rhi::PipelineStage src_stage,
                               rhi::AccessFlags src_access, rhi::PipelineStage dst_stage,
                               rhi::AccessFlags dst_access, size_t offset, size_t size) {
  VkPipelineStageFlags2 src_st = convert(src_stage);
  VkAccessFlags2 src_acc = convert(src_access);
  VkPipelineStageFlags2 dst_st = convert(dst_stage);
  VkAccessFlags2 dst_acc = convert(dst_access);
  augment_memory_barrier2_stages_for_access(src_st, src_acc, dst_st, dst_acc);
  auto* buf_obj = device_->get_buf(buf);
  ASSERT(buf_obj);
  buf_barriers_.emplace_back(VkBufferMemoryBarrier2{
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
      .srcStageMask = src_st,
      .srcAccessMask = src_acc,
      .dstStageMask = dst_st,
      .dstAccessMask = dst_acc,
      .buffer = ((VulkanBuffer*)buf_obj)->buffer(),
      .offset = offset,
      .size = size,
  });
}

void VulkanCmdEncoder::barrier(rhi::TextureHandle tex, rhi::PipelineStage src_stage,
                               rhi::AccessFlags src_access, rhi::PipelineStage dst_stage,
                               rhi::AccessFlags dst_access, int32_t base_mip_level,
                               int32_t base_array_layer) {
  barrier(tex, src_stage, src_access, dst_stage, dst_access, rhi::ResourceLayout::Undefined,
          rhi::ResourceLayout::Undefined, base_mip_level, base_array_layer);
}

void VulkanCmdEncoder::barrier(rhi::TextureHandle tex, rhi::PipelineStage src_stage,
                               rhi::AccessFlags src_access, rhi::PipelineStage dst_stage,
                               rhi::AccessFlags dst_access, rhi::ResourceLayout src_layout,
                               rhi::ResourceLayout dst_layout, int32_t base_mip_level,
                               int32_t base_array_layer) {
  VkImageAspectFlags aspect_mask = 0;
  auto* texture = (VulkanTexture*)device_->get_tex(tex);

  bool is_depth_stencil = false;
  if (is_depth_format(texture->desc().format)) {
    aspect_mask |= VK_IMAGE_ASPECT_DEPTH_BIT;
    is_depth_stencil = true;
  }
  if (is_stencil_format(texture->desc().format)) {
    aspect_mask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    is_depth_stencil = true;
  }
  if (!is_depth_stencil) {
    aspect_mask |= VK_IMAGE_ASPECT_COLOR_BIT;
  }

  VkPipelineStageFlags2 src_st = convert(src_stage);
  VkAccessFlags2 src_acc = convert(src_access);
  VkPipelineStageFlags2 dst_st = convert(dst_stage);
  VkAccessFlags2 dst_acc = convert(dst_access);
  augment_memory_barrier2_stages_for_access(src_st, src_acc, dst_st, dst_acc);

  VkImageLayout old_layout = convert(src_layout);
  VkImageLayout new_layout = convert(dst_layout);
  if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
    old_layout = layout_from_vk_access(src_acc);
  }
  if (new_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
    new_layout = layout_from_vk_access(dst_acc);
  }
  // Prefer tracked layouts when known so oldLayout matches validation / GPU reality.
  if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
    if (base_mip_level < 0) {
      VkImageLayout uniform = texture->uniform_mip_layout_or_undefined();
      if (uniform != VK_IMAGE_LAYOUT_UNDEFINED) {
        old_layout = uniform;
      }
    } else {
      VkImageLayout ml = texture->mip_layout(static_cast<uint32_t>(base_mip_level));
      if (ml != VK_IMAGE_LAYOUT_UNDEFINED) {
        old_layout = ml;
      }
    }
  }

  const uint32_t base_mip_u = base_mip_level < 0 ? 0u : static_cast<uint32_t>(base_mip_level);
  const uint32_t level_count = base_mip_level < 0 ? VK_REMAINING_MIP_LEVELS : 1u;
  const uint32_t base_layer_u = base_array_layer < 0 ? 0u : static_cast<uint32_t>(base_array_layer);
  const uint32_t layer_count = base_array_layer < 0 ? VK_REMAINING_ARRAY_LAYERS : 1u;

  img_barriers_.emplace_back(VkImageMemoryBarrier2{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = src_st,
      .srcAccessMask = src_acc,
      .dstStageMask = dst_st,
      .dstAccessMask = dst_acc,
      .oldLayout = old_layout,
      .newLayout = new_layout,
      .image = texture->image(),
      .subresourceRange = VkImageSubresourceRange{.aspectMask = aspect_mask,
                                                  .baseMipLevel = base_mip_u,
                                                  .levelCount = level_count,
                                                  .baseArrayLayer = base_layer_u,
                                                  .layerCount = layer_count},
  });
  if (new_layout != VK_IMAGE_LAYOUT_UNDEFINED) {
    if (base_mip_level < 0) {
      texture->set_all_mip_layouts(new_layout);
    } else {
      texture->set_mip_layout(static_cast<uint32_t>(base_mip_level), new_layout);
    }
  }
}

void VulkanCmdEncoder::flush_barriers() {
  if (!buf_barriers_.empty() || !img_barriers_.empty()) {
    // for (const auto& buf_barrier : buf_barriers_) {
    // LINFO("buf_barrier: src_stage={} src_access={} dst_stage={} dst_access={}",
    //       string_VkPipelineStageFlags2(buf_barrier.srcStageMask),
    //       string_VkAccessFlags2(buf_barrier.srcAccessMask),
    //       string_VkPipelineStageFlags2(buf_barrier.dstStageMask),
    //       string_VkAccessFlags2(buf_barrier.dstAccessMask));
    // }
    // for (const auto& img_barrier : img_barriers_) {
    // std::println(
    //     "img_barrier: {} {} {} {} {} {}",
    //     string_VkPipelineStageFlags2(img_barrier.srcStageMask),
    //     string_VkAccessFlags2(img_barrier.srcAccessMask),
    //     string_VkPipelineStageFlags2(img_barrier.dstStageMask),
    //     string_VkAccessFlags2(img_barrier.dstAccessMask),
    //     string_VkImageLayout(img_barrier.oldLayout),
    //     string_VkImageLayout(img_barrier.newLayout));
    // }
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

void VulkanCmdEncoder::barrier(rhi::GPUBarrier* gpu_barrier, size_t barrier_count) {
  for (size_t i = 0; i < barrier_count; i++) {
    auto& gpu_barr = gpu_barrier[i];
    if (gpu_barr.type == rhi::GPUBarrier::Type::Buffer) {
      auto& buf_barr = gpu_barr.buf;
      auto [src_stage, src_access] = convert_pipeline_stage_and_access(buf_barr.src_state);
      auto [dst_stage, dst_access] = convert_pipeline_stage_and_access(buf_barr.dst_state);
      augment_memory_barrier2_stages_for_access(src_stage, src_access, dst_stage, dst_access);
      buf_barriers_.emplace_back(VkBufferMemoryBarrier2{
          .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
          .srcStageMask = src_stage,
          .srcAccessMask = src_access,
          .dstStageMask = dst_stage,
          .dstAccessMask = dst_access,
          .buffer = ((VulkanBuffer*)device_->get_buf(buf_barr.buffer))->buffer(),
          .offset = buf_barr.offset,
          .size = buf_barr.size,
      });
    } else {
      auto& img_barr = gpu_barr.tex;
      auto [src_stage, src_access] = convert_pipeline_stage_and_access(img_barr.src_layout);
      auto [dst_stage, dst_access] = convert_pipeline_stage_and_access(img_barr.dst_layout);
      augment_memory_barrier2_stages_for_access(src_stage, src_access, dst_stage, dst_access);
      const VkImageLayout new_layout = convert_layout(img_barr.dst_layout);
      img_barriers_.emplace_back(VkImageMemoryBarrier2{
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
          .srcStageMask = src_stage,
          .srcAccessMask = src_access,
          .dstStageMask = dst_stage,
          .dstAccessMask = dst_access,
          .oldLayout = convert_layout(img_barr.src_layout),
          .newLayout = new_layout,
          .image = ((VulkanTexture*)device_->get_tex(img_barr.texture))->image(),
          .subresourceRange =
              {.aspectMask = convert(img_barr.aspect),
               .baseMipLevel = img_barr.mip == rhi::k_gpu_barrier_mip_all ? 0 : img_barr.mip,
               .levelCount =
                   img_barr.mip == rhi::k_gpu_barrier_mip_all ? VK_REMAINING_MIP_LEVELS : 1u,
               .baseArrayLayer =
                   img_barr.slice == rhi::k_gpu_barrier_slice_all ? 0u : img_barr.slice,
               .layerCount =
                   img_barr.slice == rhi::k_gpu_barrier_slice_all ? VK_REMAINING_ARRAY_LAYERS : 1u},
      });
      auto* vk_tex = (VulkanTexture*)device_->get_tex(img_barr.texture);
      if (img_barr.mip == rhi::k_gpu_barrier_mip_all) {
        vk_tex->set_all_mip_layouts(new_layout);
      } else {
        vk_tex->set_mip_layout(img_barr.mip, new_layout);
      }
    }
  }
}

void VulkanCmdEncoder::bind_uav(rhi::TextureHandle texture, uint32_t slot, int subresource_id) {
  binding_table_.UAV[slot] = texture.to64();
  binding_table_.UAV_subresources[slot] = subresource_id;
  descriptors_dirty_ = true;
}

void VulkanCmdEncoder::bind_uav(rhi::BufferHandle buffer, uint32_t slot, size_t offset_bytes) {
  binding_table_.UAV[slot] = buffer.to64();
  binding_table_.UAV_subresources[slot] = rhi::DescriptorBindingTable::k_buffer_resource;
  binding_table_.UAV_offsets[slot] = offset_bytes;
  descriptors_dirty_ = true;
}

void VulkanCmdEncoder::bind_cbv(rhi::BufferHandle buffer, uint32_t slot, size_t offset_bytes,
                                size_t size_bytes) {
  binding_table_.CBV[slot] = buffer;
  binding_table_.CBV_offsets[slot] = offset_bytes;
  binding_table_.CBV_sizes[slot] = size_bytes;
  descriptors_dirty_ = true;
}

void VulkanCmdEncoder::flush_binds() {
  ASSERT(bound_pipeline_);
  if (descriptors_dirty_ && !bound_pipeline_->layout_bindings_.empty()) {
    ASSERT(bound_pipeline_->descriptor_set_layout_);

    VkDescriptorSetAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = binder_pools_[curr_frame_i_].pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &bound_pipeline_->descriptor_set_layout_,
    };

    VkDescriptorSet descriptor_set;
    VkResult result;
    do {
      result = vkAllocateDescriptorSets(vk_device_, &alloc_info, &descriptor_set);
      if (result == VK_ERROR_OUT_OF_POOL_MEMORY) {
        binder_pools_[curr_frame_i_].pool_size *= 2;
        binder_pools_[curr_frame_i_].init(*device_);
      }
    } while (result == VK_ERROR_OUT_OF_POOL_MEMORY);
    constexpr uint32_t k_uav_binding_start = 1000;
    constexpr uint32_t k_srv_binding_start = 2000;
    constexpr uint32_t k_cbv_binding_start = 3000;
    binder_.writes.clear();
    binder_.img_infos.clear();
    binder_.buf_infos.clear();

    for (auto& binding : bound_pipeline_->layout_bindings_) {
      if (binding.pImmutableSamplers) {
        continue;
      }
      auto& write = binder_.writes.emplace_back();
      write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.pNext = nullptr;
      write.dstSet = descriptor_set;
      write.dstBinding = binding.binding;
      write.dstArrayElement = 0;
      ASSERT(binding.descriptorCount == 1);
      write.descriptorCount = binding.descriptorCount;
      write.descriptorType = binding.descriptorType;
      if (binding.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
        if (binding.binding >= k_srv_binding_start) {
          // srv
          auto table_index = binding.binding - k_srv_binding_start;
          auto tex_handle = rhi::TextureHandle{binding_table_.SRV[table_index]};
          ASSERT((table_index >= 0 && table_index < ARRAY_SIZE(binding_table_.SRV)));
          auto subresource_id = binding_table_.SRV_subresources[table_index];
          auto& img_info = binder_.img_infos.emplace_back();
          img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          img_info.imageView = device_->get_vk_tex_view(tex_handle, subresource_id);
          write.pImageInfo = &img_info;
        } else if (binding.binding >= k_uav_binding_start) {
          // uav
          auto table_index = binding.binding - k_uav_binding_start;
          auto tex_handle = rhi::TextureHandle{binding_table_.UAV[table_index]};
          ASSERT((table_index >= 0 && table_index < ARRAY_SIZE(binding_table_.UAV)));
          auto subresource_id = binding_table_.UAV_subresources[table_index];
          auto& img_info = binder_.img_infos.emplace_back();
          img_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
          img_info.imageView = device_->get_vk_tex_view(tex_handle, subresource_id);
          write.pImageInfo = &img_info;
        } else {
          ASSERT(0);
        }
      } else if (binding.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) {
        if (binding.binding >= k_srv_binding_start) {
          // srv
          auto table_index = binding.binding - k_srv_binding_start;
          auto tex_handle = rhi::TextureHandle{binding_table_.SRV[table_index]};
          ASSERT((table_index >= 0 && table_index < ARRAY_SIZE(binding_table_.SRV)));
          auto subresource_id = binding_table_.SRV_subresources[table_index];
          auto& img_info = binder_.img_infos.emplace_back();
          img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          img_info.imageView = device_->get_vk_tex_view(tex_handle, subresource_id);
          write.pImageInfo = &img_info;
        } else if (binding.binding >= k_uav_binding_start) {
          // uav
          auto table_index = binding.binding - k_uav_binding_start;
          auto tex_handle = rhi::TextureHandle{binding_table_.UAV[table_index]};
          ASSERT((table_index >= 0 && table_index < ARRAY_SIZE(binding_table_.UAV)));
          auto subresource_id = binding_table_.UAV_subresources[table_index];
          auto& img_info = binder_.img_infos.emplace_back();
          img_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
          img_info.imageView = device_->get_vk_tex_view(tex_handle, subresource_id);
          write.pImageInfo = &img_info;
        } else {
          ASSERT(0);
        }
      } else if (binding.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
        if (binding.binding >= k_srv_binding_start) {
          // srv
          auto table_index = binding.binding - k_srv_binding_start;
          auto buf_handle = rhi::BufferHandle{binding_table_.SRV[table_index]};
          ASSERT((table_index >= 0 && table_index < ARRAY_SIZE(binding_table_.SRV)));
          auto& buf_info = binder_.buf_infos.emplace_back();
          auto* buf = device_->get_vk_buf(buf_handle);
          buf_info.buffer = buf->buffer_;
          buf_info.offset = binding_table_.SRV_offsets[table_index];
          buf_info.range = VK_WHOLE_SIZE;
          write.pBufferInfo = &buf_info;
        } else if (binding.binding >= k_uav_binding_start) {
          // uav
          auto table_index = binding.binding - k_uav_binding_start;
          auto buf_handle = rhi::BufferHandle{binding_table_.UAV[table_index]};
          ASSERT((table_index >= 0 && table_index < ARRAY_SIZE(binding_table_.UAV)));
          auto& buf_info = binder_.buf_infos.emplace_back();
          auto* buf = device_->get_vk_buf(buf_handle);
          buf_info.buffer = buf->buffer_;
          buf_info.offset = binding_table_.UAV_offsets[table_index];
          buf_info.range = VK_WHOLE_SIZE;
          write.pBufferInfo = &buf_info;
        } else {
          ASSERT(0);
        }
      } else if (binding.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
        ASSERT(binding.binding >= k_cbv_binding_start);
        auto table_index = binding.binding - k_cbv_binding_start;
        auto buf_handle = rhi::BufferHandle{binding_table_.CBV[table_index]};
        ASSERT((table_index >= 0 && table_index < ARRAY_SIZE(binding_table_.CBV)));
        auto& buf_info = binder_.buf_infos.emplace_back();
        auto* buf = device_->get_vk_buf(buf_handle);
        ASSERT(buf);
        buf_info.buffer = buf->buffer_;
        buf_info.offset = binding_table_.CBV_offsets[table_index];
        buf_info.range = binding_table_.CBV_sizes[table_index];
        write.pBufferInfo = &buf_info;
      } else if (binding.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER) {
        LINFO("Failed descriptor type: {}", string_VkDescriptorType(binding.descriptorType));
        LINFO("Binding: {}", binding.binding);
        ASSERT(0);
      } else {
        LINFO("unhandled descriptor type: {}", string_VkDescriptorType(binding.descriptorType));
        ASSERT(0);
      }
    }
    ASSERT(!binder_.writes.empty());
    vkUpdateDescriptorSets(vk_device_, binder_.writes.size(), binder_.writes.data(), 0, nullptr);
    vkCmdBindDescriptorSets(cmd(), get_bound_pipeline_bind_point(), bound_pipeline_->layout_, 0, 1,
                            &descriptor_set, 0, nullptr);
  }
}

void DescriptorBinderPool::init(VulkanDevice& device) {
  VkDevice vk_device = device.vk_device();

  VkDescriptorPoolSize pool_sizes[]{
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, rhi::DESCRIPTOR_TABLE_CBV_COUNT * pool_size},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, rhi::DESCRIPTOR_TABLE_CBV_COUNT * pool_size},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, rhi::TOTAL_SRV_BINDINGS * pool_size},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, rhi::TOTAL_SRV_BINDINGS * pool_size},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, rhi::TOTAL_SRV_BINDINGS * pool_size},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, rhi::TOTAL_UAV_BINDINGS * pool_size},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, rhi::TOTAL_UAV_BINDINGS * pool_size},
      // TODO: separate sampler thing in table
      {VK_DESCRIPTOR_TYPE_SAMPLER, rhi::TOTAL_UAV_BINDINGS * pool_size},
  };

  VkDescriptorPoolCreateInfo pool_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags = 0,
      .maxSets = pool_size,
      .poolSizeCount = ARRAY_SIZE(pool_sizes),
      .pPoolSizes = pool_sizes,
  };

  destroy(device);

  VK_CHECK(vkCreateDescriptorPool(vk_device, &pool_info, nullptr, &pool));
}

void DescriptorBinderPool::destroy(VulkanDevice& device) {
  VkDevice vk_device = device.vk_device();
  if (pool) {
    vkDestroyDescriptorPool(vk_device, pool, nullptr);
    pool = VK_NULL_HANDLE;
  }
}

void DescriptorBinderPool::reset(VulkanDevice& device) const {
  ASSERT(pool);
  VK_CHECK(vkResetDescriptorPool(device.vk_device(), pool, 0));
}

void VulkanCmdEncoder::dispatch_compute(glm::uvec3 thread_groups, glm::uvec3) {
  flush_barriers();
  flush_binds();
  vkCmdDispatch(cmd(), thread_groups.x, thread_groups.y, thread_groups.z);
}

[[nodiscard]] VkPipelineBindPoint VulkanCmdEncoder::get_bound_pipeline_bind_point() const {
  return bound_pipeline_->type() == rhi::PipelineType::Graphics ? VK_PIPELINE_BIND_POINT_GRAPHICS
                                                                : VK_PIPELINE_BIND_POINT_COMPUTE;
}

void VulkanCmdEncoder::set_scissor(glm::uvec2 min, glm::uvec2 extent) {
  VkRect2D scissor{
      .offset = {static_cast<int32_t>(min.x), static_cast<int32_t>(min.y)},
      .extent = {extent.x, extent.y},
  };
  vkCmdSetScissor(cmd(), 0, 1, &scissor);
}

void VulkanCmdEncoder::bind_srv(rhi::TextureHandle texture, uint32_t slot, int subresource_id) {
  ASSERT(slot < ARRAY_SIZE(binding_table_.SRV));
  binding_table_.SRV[slot] = texture.to64();
  binding_table_.SRV_subresources[slot] = subresource_id;
  descriptors_dirty_ = true;
}

void VulkanCmdEncoder::bind_srv(rhi::BufferHandle buffer, uint32_t slot, size_t offset_bytes) {
  binding_table_.SRV[slot] = buffer.to64();
  binding_table_.SRV_subresources[slot] = rhi::DescriptorBindingTable::k_buffer_resource;
  binding_table_.SRV_offsets[slot] = offset_bytes;
  descriptors_dirty_ = true;
}

void VulkanCmdEncoder::fill_buffer(rhi::BufferHandle handle, uint32_t offset_bytes, uint32_t size,
                                   uint32_t value) {
  flush_barriers();
  auto* buf = device_->get_buf(handle);
  ASSERT(buf);
  vkCmdFillBuffer(cmd(), ((VulkanBuffer*)buf)->buffer(), offset_bytes, size, value);
}

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
