#pragma once

#include <volk.h>

#include "VMAWrapper.hpp"
#include "VkBootstrap.h"
#include "core/Config.hpp"
#include "gfx/rhi/Config.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "gfx/vulkan/VulkanBuffer.hpp"
#include "gfx/vulkan/VulkanCmdEncoder.hpp"
#include "gfx/vulkan/VulkanPipeline.hpp"
#include "gfx/vulkan/VulkanSampler.hpp"
#include "gfx/vulkan/VulkanSwapchain.hpp"
#include "gfx/vulkan/VulkanTexture.hpp"

namespace TENG_NAMESPACE {

class Window;

namespace gfx::vk {

enum QueueType : uint8_t {
  QueueType_Graphics,
  QueueType_Compute,
  QueueType_Transfer,
  QueueType_Count,
};

class VulkanDevice : public rhi::Device {
 public:
  using rhi::Device::get_buf;
  using rhi::Device::get_pipeline;
  using rhi::Device::get_tex;

  void init(const InitInfo&) override;
  void shutdown() override;

  rhi::BufferHandle create_buf(const rhi::BufferDesc& desc) override;

  rhi::TextureHandle create_tex(const rhi::TextureDesc& desc) override;
  rhi::SwapchainHandle create_swapchain(const rhi::SwapchainDesc& desc) override;

  rhi::Texture* get_tex(rhi::TextureHandle handle) override { return texture_pool_.get(handle); }
  rhi::Buffer* get_buf(rhi::BufferHandle handle) override { return buffer_pool_.get(handle); }
  rhi::Pipeline* get_pipeline(rhi::PipelineHandle handle) override {
    return pipeline_pool_.get(handle);
  }

  void destroy(rhi::BufferHandle handle) override;
  void destroy(rhi::PipelineHandle handle) override;
  void destroy(rhi::TextureHandle handle) override;
  void destroy(rhi::SamplerHandle handle) override;
  void destroy(rhi::SwapchainHandle handle) override;

  rhi::PipelineHandle create_graphics_pipeline(
      const rhi::GraphicsPipelineCreateInfo& cinfo) override;

  rhi::SamplerHandle create_sampler(const rhi::SamplerDesc& desc) override {
    exit(1);
    return sampler_pool_.alloc(desc, rhi::k_invalid_bindless_idx);
  }
  [[nodiscard]] const Info& get_info() const override { return info_; }

  rhi::CmdEncoder* begin_cmd_encoder() override;
  void submit_frame() override;

  [[nodiscard]] void* get_native_device() const override { return nullptr; }

 private:
  struct Queue {
    VkQueue queue;
    uint32_t family_idx;
  };

  Info info_{};
  BlockPool<rhi::BufferHandle, VulkanBuffer> buffer_pool_{128, 1, true};
  BlockPool<rhi::TextureHandle, VulkanTexture> texture_pool_{128, 1, true};
  BlockPool<rhi::PipelineHandle, VulkanPipeline> pipeline_pool_{20, 1, true};
  BlockPool<rhi::SamplerHandle, VulkanSampler> sampler_pool_{16, 1, true};
  BlockPool<rhi::SwapchainHandle, VulkanSwapchain> swapchain_pool_{16, 1, true};
  VkCommandPool command_pools_[k_max_frames_in_flight];
  vkb::Instance vkb_inst_;
  vkb::Device vkb_device_;
  VkInstance instance_{};
  VkPhysicalDevice physical_device_{};
  VkDevice device_{};
  VkPipelineLayout default_pipeline_layout_{};

  Queue queues_[QueueType_Count]{};

  std::vector<std::unique_ptr<VulkanCmdEncoder>> cmd_encoders_;
  size_t curr_cmd_encoder_i_{};
  VmaAllocator allocator_;
  bool vsync_enabled_{};
};

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
