#pragma once

#include <volk.h>

#include "VMAWrapper.hpp"
#include "VkBootstrap.h"
#include "gfx/Config.hpp"
#include "gfx/Device.hpp"
#include "gfx/GFXTypes.hpp"
#include "gfx/vulkan/VulkanBuffer.hpp"
#include "gfx/vulkan/VulkanCmdEncoder.hpp"
#include "gfx/vulkan/VulkanPipeline.hpp"
#include "gfx/vulkan/VulkanSampler.hpp"
#include "gfx/vulkan/VulkanSwapchain.hpp"
#include "gfx/vulkan/VulkanTexture.hpp"

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

  rhi::Texture* get_tex(rhi::TextureHandle handle) override { return texture_pool_.get(handle); }
  rhi::Buffer* get_buf(rhi::BufferHandle handle) override { return buffer_pool_.get(handle); }
  rhi::Pipeline* get_pipeline(rhi::PipelineHandle handle) override {
    return pipeline_pool_.get(handle);
  }

  void destroy(rhi::BufferHandle handle) override {
    exit(1);
    buffer_pool_.destroy(handle);
  }

  void destroy(rhi::PipelineHandle handle) override {
    exit(1);
    pipeline_pool_.destroy(handle);
  }

  rhi::PipelineHandle create_graphics_pipeline(
      const rhi::GraphicsPipelineCreateInfo& /*cinfo*/) override;

  rhi::SamplerHandle create_sampler(const rhi::SamplerDesc& desc) override {
    exit(1);
    return sampler_pool_.alloc(desc, rhi::k_invalid_bindless_idx);
  }
  [[nodiscard]] const Info& get_info() const override { return info_; }

  // TODO: is there a better spot for setting window dims, ie on event
  bool begin_frame(glm::uvec2) override { exit(1); }
  void copy_to_buffer(const void* /*src*/, size_t /*src_size*/, rhi::BufferHandle /*buf*/,
                      size_t /*dst_offset*/) override {
    exit(1);
  }

  // commands
  rhi::CmdEncoder* begin_command_list() override;
  void submit_frame() override {}

  void destroy(rhi::TextureHandle handle) override {
    exit(1);
    texture_pool_.destroy(handle);
  }

  void destroy(rhi::SamplerHandle handle) override {
    exit(1);
    sampler_pool_.destroy(handle);
  }

  rhi::Swapchain& get_swapchain() override { return swapchain_; }

  [[nodiscard]] const rhi::Swapchain& get_swapchain() const override { return swapchain_; }

  [[nodiscard]] void* get_native_device() const override {
    exit(1);
    return nullptr;
  }

  void set_vsync(bool) override { exit(1); }

  bool get_vsync() const override {
    exit(1);
    return vsync_enabled_;
  }

  void fill_buffer(rhi::BufferHandle /*handle*/, size_t /*size*/, size_t /*offset*/,
                   uint32_t /*fill_value*/) override {
    exit(1);
  }

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
  VkCommandPool command_pools_[k_max_frames_in_flight];
  VulkanSwapchain swapchain_;
  vkb::Instance vkb_inst_;
  vkb::Device vkb_device_;
  VkInstance instance_{};
  VkSurfaceKHR surface_{};
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
