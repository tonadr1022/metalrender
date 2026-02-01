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
  rhi::TextureViewHandle create_tex_view(rhi::TextureHandle /*handle*/, uint32_t /*base_mip_level*/,
                                         uint32_t /*level_count*/, uint32_t /*base_array_layer*/,
                                         uint32_t /*layer_count*/) override {
    exit(1);
  }
  rhi::QueryPoolHandle create_query_pool(const rhi::QueryPoolDesc& /*desc*/) override { exit(1); }

  rhi::Texture* get_tex(rhi::TextureHandle handle) override { return texture_pool_.get(handle); }
  rhi::Buffer* get_buf(rhi::BufferHandle handle) override { return buffer_pool_.get(handle); }
  rhi::Pipeline* get_pipeline(rhi::PipelineHandle handle) override {
    return pipeline_pool_.get(handle);
  }
  rhi::Swapchain* get_swapchain(rhi::SwapchainHandle /*handle*/) override { exit(1); }
  void get_all_buffers(std::vector<rhi::Buffer*>& /*out_buffers*/) override { exit(1); }
  uint32_t get_tex_view_bindless_idx(rhi::TextureHandle /*handle*/,
                                     int /*subresource_id*/) override {
    exit(1);
  }

  void destroy(rhi::BufferHandle handle) override;
  void destroy(rhi::PipelineHandle handle) override;
  void destroy(rhi::TextureHandle handle) override;
  void destroy(rhi::SamplerHandle handle) override;
  void destroy(rhi::SwapchainHandle handle) override;
  void destroy(rhi::TextureHandle /*tex_handle*/, int /*tex_view_handle*/) override { exit(1); }
  void destroy(rhi::QueryPoolHandle /*handle*/) override { exit(1); }

  rhi::PipelineHandle create_graphics_pipeline(
      const rhi::GraphicsPipelineCreateInfo& cinfo) override;
  rhi::PipelineHandle create_compute_pipeline(const rhi::ShaderCreateInfo& /*cinfo*/) override {
    exit(1);
  }
  bool replace_pipeline(rhi::PipelineHandle /*handle*/,
                        const rhi::GraphicsPipelineCreateInfo& /*cinfo*/) override {
    exit(1);
  }
  bool replace_compute_pipeline(rhi::PipelineHandle /*handle*/,
                                const rhi::ShaderCreateInfo& /*cinfo*/) override {
    exit(1);
  }

  rhi::SamplerHandle create_sampler(const rhi::SamplerDesc& desc) override {
    exit(1);
    return sampler_pool_.alloc(desc, rhi::k_invalid_bindless_idx);
  }
  [[nodiscard]] const Info& get_info() const override { return info_; }

  rhi::CmdEncoder* begin_cmd_encoder() override;
  void submit_frame() override;

  void cmd_encoder_wait_for(rhi::CmdEncoder* /*cmd_enc*/, rhi::CmdEncoder* /*wait_for*/) override {
    exit(1);
  }
  bool recreate_swapchain(const rhi::SwapchainDesc& /*desc*/,
                          rhi::Swapchain* /*swapchain*/) override {
    exit(1);
  }
  void begin_swapchain_rendering(rhi::Swapchain* /*swapchain*/, rhi::CmdEncoder* /*cmd_enc*/,
                                 glm::vec4* /*clear_color*/) override {
    exit(1);
  }
  void resolve_query_data(rhi::QueryPoolHandle /*query_pool*/, uint32_t /*start_query*/,
                          uint32_t /*query_count*/,
                          std::span<uint64_t> /*out_timestamps*/) override {
    exit(1);
  }

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
};

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
