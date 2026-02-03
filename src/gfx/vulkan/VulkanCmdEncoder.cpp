#include "VulkanCmdEncoder.hpp"

#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "gfx/metal/RootLayout.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "gfx/vulkan/VkUtil.hpp"
#include "gfx/vulkan/VulkanCommon.hpp"
#include "gfx/vulkan/VulkanDevice.hpp"
#include "gfx/vulkan/VulkanTexture.hpp"
#include "vulkan/vk_enum_string_helper.h"

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

VulkanCmdEncoder::VulkanCmdEncoder(VulkanDevice* device)
    : device_(device), vk_device_(device_->vk_device()) {}

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

void VulkanCmdEncoder::bind_pipeline(rhi::PipelineHandle handle) {
  auto* pipeline = (VulkanPipeline*)device_->get_pipeline(handle);
  bound_pipeline_ = pipeline;
  ASSERT(pipeline);
  vkCmdBindPipeline(cmd(), get_bound_pipeline_bind_point(), pipeline->pipeline_);
}

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
  ASSERT(bound_pipeline_);
  ASSERT(bound_pipeline_->layout_);
  vkCmdPushConstants(cmd(), bound_pipeline_->layout_, VK_SHADER_STAGE_ALL, 0, (uint32_t)size, data);
}

void VulkanCmdEncoder::end_encoding() {
  flush_barriers();
  VK_CHECK(vkEndCommandBuffer(cmd()));
}

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

namespace {
std::pair<VkPipelineStageFlags2, VkAccessFlags2> convert_pipeline_stage_and_access(
    rhi::ResourceState state) {
  VkPipelineStageFlags2 stage{};
  VkAccessFlags2 access{};

  if (state & rhi::ColorWrite) {
    stage |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    access |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  }
  if (state & rhi::ColorRead) {
    stage |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    access |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
  }
  if (state & rhi::DepthStencilRead) {
    stage |=
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    access |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
  }
  if (state & rhi::DepthStencilWrite) {
    stage |=
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    access |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  }
  if (state & rhi::VertexRead) {
    stage |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    access |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
  }
  if (state & rhi::IndexRead) {
    stage |= VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
    access |= VK_ACCESS_2_INDEX_READ_BIT;
  }
  if (state & rhi::IndirectRead) {
    stage |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    access |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
  }
  if (state & rhi::ComputeRead) {
    stage |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    access |= VK_ACCESS_2_SHADER_READ_BIT;
  }
  if (state & rhi::ComputeWrite) {
    stage |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    access |= VK_ACCESS_2_SHADER_WRITE_BIT;
  }
  if (state & rhi::TransferRead) {
    stage |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    access |= VK_ACCESS_2_TRANSFER_READ_BIT;
  }
  if (state & rhi::TransferWrite) {
    stage |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    access |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
  }
  if (state & rhi::FragmentStorageRead) {
    stage |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    access |= VK_ACCESS_2_SHADER_READ_BIT;
  }
  if (state & rhi::ComputeSample) {
    stage |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    access |= VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
  }
  if (state & rhi::FragmentSample) {
    stage |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    access |= VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
  }
  if (state & rhi::ShaderRead) {
    stage |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    access |= VK_ACCESS_2_SHADER_READ_BIT;
  }
  if (state & rhi::SwapchainPresent) {
    stage |= VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    access |= 0;
  }

  return {stage, access};
}

VkImageLayout convert_layout(rhi::ResourceState state) {
  if (state & rhi::ColorWrite || state & rhi::ColorRead) {
    return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  }
  if (state & rhi::DepthStencilWrite) {
    return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  }
  if (state & rhi::DepthStencilRead) {
    return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
  }
  if (state & rhi::ShaderRead) {
    return VK_IMAGE_LAYOUT_GENERAL;
  }
  if (state & rhi::ComputeWrite || state & rhi::ComputeRead) {
    return VK_IMAGE_LAYOUT_GENERAL;
  }
  if (state & rhi::TransferRead) {
    return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  }
  if (state & rhi::TransferWrite) {
    return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  }
  if (state & rhi::SwapchainPresent) {
    return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  }
  return VK_IMAGE_LAYOUT_GENERAL;
}

}  // namespace

void VulkanCmdEncoder::barrier(rhi::GPUBarrier* gpu_barrier, size_t barrier_count) {
  for (size_t i = 0; i < barrier_count; i++) {
    auto& gpu_barr = gpu_barrier[i];
    if (gpu_barr.type == rhi::GPUBarrier::Type::Buffer) {
      auto& buf_barr = gpu_barr.buf;
      auto [src_stage, src_access] = convert_pipeline_stage_and_access(buf_barr.src_state);
      auto [dst_stage, dst_access] = convert_pipeline_stage_and_access(buf_barr.dst_state);
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
      img_barriers_.emplace_back(VkImageMemoryBarrier2{
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
          .srcStageMask = src_stage,
          .srcAccessMask = src_access,
          .dstStageMask = dst_stage,
          .dstAccessMask = dst_access,
          .oldLayout = convert_layout(img_barr.src_layout),
          .newLayout = convert_layout(img_barr.dst_layout),
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

void VulkanCmdEncoder::bind_uav(rhi::TextureHandle texture, uint32_t slot, int subresource_id) {
  binding_table_.UAV[slot] = texture.to64();
  binding_table_.UAV_subresources[slot] = subresource_id;
  descriptors_dirty_ = true;
}

void VulkanCmdEncoder::flush_binds() {
  ASSERT(bound_pipeline_);
  if (descriptors_dirty_) {
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
    binder_.writes.clear();

    for (auto& binding : bound_pipeline_->layout_bindings_) {
      auto& write = binder_.writes.emplace_back();
      write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.pNext = nullptr;
      // TODO: lollll
      write.dstSet = descriptor_set;
      write.dstBinding = binding.binding;
      write.dstArrayElement = 0;
      ASSERT(binding.descriptorCount == 1);
      write.descriptorCount = binding.descriptorCount;
      write.descriptorType = binding.descriptorType;
      if (binding.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
        auto table_index = binding.binding - k_uav_binding_start;
        auto tex_handle = rhi::TextureHandle{binding_table_.UAV[table_index]};
        ASSERT((table_index >= 0 && table_index < ARRAY_SIZE(binding_table_.UAV)));
        auto subresource_id = binding_table_.UAV_subresources[table_index];
        auto& img_info = binder_.img_infos.emplace_back();
        img_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        auto* tex = device_->get_vk_tex(tex_handle);
        if (subresource_id == -1) {
          img_info.imageView = tex->default_view_;
        } else {
          // not handled image views yet
          ASSERT(subresource_id == -1);
        }

        write.pImageInfo = &img_info;

      } else {
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
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, DESCRIPTOR_TABLE_CBV_COUNT * pool_size},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, DESCRIPTOR_TABLE_CBV_COUNT * pool_size},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, TOTAL_SRV_BINDINGS * pool_size},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, TOTAL_SRV_BINDINGS * pool_size},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, TOTAL_SRV_BINDINGS * pool_size},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, TOTAL_UAV_BINDINGS * pool_size},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, TOTAL_UAV_BINDINGS * pool_size},
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

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
