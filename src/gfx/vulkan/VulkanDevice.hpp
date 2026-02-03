#pragma once

#include <vulkan/vulkan_core.h>

#include "VMAWrapper.hpp"
#include "VkBootstrap.h"
#include "core/Config.hpp"
#include "gfx/rhi/Config.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "gfx/rhi/Queue.hpp"
#include "gfx/vulkan/VulkanBuffer.hpp"
#include "gfx/vulkan/VulkanCmdEncoder.hpp"
#include "gfx/vulkan/VulkanDeleteQueue.hpp"
#include "gfx/vulkan/VulkanPipeline.hpp"
#include "gfx/vulkan/VulkanSampler.hpp"
#include "gfx/vulkan/VulkanSwapchain.hpp"
#include "gfx/vulkan/VulkanTexture.hpp"

namespace TENG_NAMESPACE {

class Window;

namespace gfx::vk {

class VulkanDevice : public rhi::Device {
 public:
  using rhi::Device::get_buf;
  using rhi::Device::get_pipeline;
  using rhi::Device::get_tex;

  void init(const InitInfo&) override;
  void shutdown() override;
  rhi::ShaderTarget get_supported_shader_targets() override { return rhi::ShaderTarget::Spirv; }

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
  rhi::Swapchain* get_swapchain(rhi::SwapchainHandle handle) override {
    return swapchain_pool_.get(handle);
  }
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
  rhi::PipelineHandle create_compute_pipeline(const rhi::ShaderCreateInfo& cinfo) override;
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

  bool recreate_swapchain(const rhi::SwapchainDesc& desc, rhi::Swapchain* swapchain) override;

  void begin_swapchain_rendering(rhi::Swapchain* swapchain, rhi::CmdEncoder* cmd_enc,
                                 glm::vec4* clear_color) override;
  void resolve_query_data(rhi::QueryPoolHandle /*query_pool*/, uint32_t /*start_query*/,
                          uint32_t /*query_count*/,
                          std::span<uint64_t> /*out_timestamps*/) override {
    exit(1);
  }

  [[nodiscard]] void* get_native_device() const override { return device_; }

  [[nodiscard]] VkDevice vk_device() const { return device_; }
  VulkanTexture* get_vk_tex(rhi::TextureHandle handle) {
    return static_cast<VulkanTexture*>(get_tex(handle));
  }

 private:
  size_t frame_idx() const { return frame_num_ % info_.frames_in_flight; }
  // TODO: doesn't handle arrays.
  VkImageView create_img_view(VulkanTexture& img, VkImageViewType type,
                              const VkImageSubresourceRange& subresource_range);
  void acquire_next_swapchain_image(VulkanSwapchain& swapchain, uint32_t& out_img_idx,
                                    VkSemaphore& out_acquire_semaphore);
  void set_vk_debug_name(VkObjectType object_type, uint64_t object_handle, const char* name);
  VkSemaphore create_semaphore(const char* name = nullptr);

  struct Queue {
    VkQueue queue;
    uint32_t family_idx;
    std::vector<VkSemaphoreSubmitInfo> wait_semaphores;
    std::vector<VkSemaphoreSubmitInfo> signal_semaphores;
    std::vector<VkCommandBufferSubmitInfo> submit_cmd_bufs;
    std::vector<VkSwapchainKHR> present_swapchains;
    std::vector<VkSemaphore> present_wait_semaphores;
    std::vector<uint32_t> present_swapchain_img_indices;
    [[nodiscard]] bool is_valid() const { return queue != VK_NULL_HANDLE; }
    void submit(VkFence fence);
  };

  Info info_{};
  BlockPool<rhi::BufferHandle, VulkanBuffer> buffer_pool_{128, 1, true};
  BlockPool<rhi::TextureHandle, VulkanTexture> texture_pool_{128, 1, true};
  BlockPool<rhi::PipelineHandle, VulkanPipeline> pipeline_pool_{20, 1, true};
  BlockPool<rhi::SamplerHandle, VulkanSampler> sampler_pool_{16, 1, true};
  BlockPool<rhi::SwapchainHandle, VulkanSwapchain> swapchain_pool_{16, 1, true};
  DeleteQueue del_q_{};
  VkCommandPool command_pools_[k_max_frames_in_flight];
  vkb::Instance vkb_inst_;
  vkb::Device vkb_device_;
  VkInstance instance_{};
  VkPhysicalDevice physical_device_{};
  VkDevice device_{};
  VkPipelineLayout default_pipeline_layout_{};
  VkFence frame_fences_[(int)rhi::QueueType::Count][k_max_frames_in_flight]{};

  Queue queues_[(int)rhi::QueueType::Count]{};

  std::vector<std::unique_ptr<VulkanCmdEncoder>> cmd_encoders_;
  size_t curr_cmd_encoder_i_{};
  VmaAllocator allocator_;
  size_t frame_num_{};
  std::filesystem::path shader_lib_dir_;
  template <typename T>
  struct DeleteQueueEntry {
    T obj;
    size_t frame_to_delete;
  };

  VkShaderModule create_shader_module(const std::filesystem::path& path);
  VkShaderModule create_shader_module(std::span<const uint32_t> spirv_code);
  std::unordered_map<uint64_t, VkDescriptorSetLayout> set_layout_cache_;
  uint64_t hash_descriptor_set_layout_cinfo(const VkDescriptorSetLayoutCreateInfo& cinfo);
};

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
