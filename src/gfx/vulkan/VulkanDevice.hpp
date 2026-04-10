#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <mutex>
#include <vector>

#include "VMAWrapper.hpp"
#include "VkBootstrap.h"
#include "core/Config.hpp"
#include "core/Logger.hpp"
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
  rhi::TextureViewHandle create_tex_view(rhi::TextureHandle handle, uint32_t base_mip_level,
                                         uint32_t level_count, uint32_t base_array_layer,
                                         uint32_t layer_count) override;

  rhi::QueryPoolHandle create_query_pool([[maybe_unused]] const rhi::QueryPoolDesc& desc) override {
    LERROR("VulkanDevice::create_query_pool not implemented");
    return {};
  }

  rhi::Texture* get_tex(rhi::TextureHandle handle) override { return texture_pool_.get(handle); }
  rhi::Buffer* get_buf(rhi::BufferHandle handle) override { return buffer_pool_.get(handle); }
  rhi::Pipeline* get_pipeline(rhi::PipelineHandle handle) override {
    return pipeline_pool_.get(handle);
  }

  rhi::Swapchain* get_swapchain(rhi::SwapchainHandle handle) override {
    return swapchain_pool_.get(handle);
  }

  void get_all_buffers(std::vector<rhi::Buffer*>& out_buffers) override {
    buffer_pool_.for_each([&out_buffers](const VulkanBuffer& buffer) {
      out_buffers.emplace_back((rhi::Buffer*)&buffer);
    });
  }

  uint32_t get_tex_view_bindless_idx(rhi::TextureHandle handle, int subresource_id) override;

  void destroy(rhi::BufferHandle handle) override;
  void destroy(rhi::PipelineHandle handle) override;
  void destroy(rhi::TextureHandle handle) override;
  void destroy(rhi::SamplerHandle handle) override;
  void destroy(rhi::SwapchainHandle handle) override;
  void destroy(rhi::TextureHandle tex_handle, int tex_view_handle) override;
  void destroy([[maybe_unused]] rhi::QueryPoolHandle handle) override {
    LERROR("VulkanDevice::destroy(QueryPoolHandle) not implemented");
  }

  rhi::PipelineHandle create_graphics_pipeline(
      const rhi::GraphicsPipelineCreateInfo& cinfo) override;
  rhi::PipelineHandle create_compute_pipeline(const rhi::ShaderCreateInfo& cinfo) override;
  bool replace_pipeline(rhi::PipelineHandle /*handle*/,
                        const rhi::GraphicsPipelineCreateInfo& /*cinfo*/) override {
    ASSERT(0);
    return false;
  }

  bool replace_compute_pipeline(rhi::PipelineHandle /*handle*/,
                                const rhi::ShaderCreateInfo& /*cinfo*/) override {
    ASSERT(0);
    return false;
  }

  rhi::SamplerHandle create_sampler(const rhi::SamplerDesc& desc) override;
  [[nodiscard]] const Info& get_info() const override { return info_; }

  rhi::CmdEncoder* begin_cmd_encoder(rhi::QueueType queue_type) override;
  void submit_frame() override;
  void immediate_submit(rhi::QueueType, ImmediateSubmitFn&&) override { ASSERT(0); }

  void cmd_encoder_wait_for(rhi::CmdEncoder* /*cmd_enc*/, rhi::CmdEncoder* /*wait_for*/) override {
    //    ASSERT(0);
  }

  bool recreate_swapchain(const rhi::SwapchainDesc& desc, rhi::Swapchain* swapchain) override;

  void enqueue_swapchain_for_present(rhi::Swapchain* swapchain, rhi::CmdEncoder* cmd_enc) override;
  void begin_swapchain_rendering(rhi::Swapchain* swapchain, rhi::CmdEncoder* cmd_enc,
                                 glm::vec4* clear_color) override;
  void acquire_next_swapchain_image(rhi::Swapchain* swapchain) override;
  void resolve_query_data(rhi::QueryPoolHandle /*query_pool*/, uint32_t /*start_query*/,
                          uint32_t /*query_count*/,
                          std::span<uint64_t> /*out_timestamps*/) override {
    // ASSERT(0);
  }

  [[nodiscard]] void* get_native_device() const override { return device_; }

  [[nodiscard]] VkDevice vk_device() const { return device_; }
  VulkanTexture* get_vk_tex(rhi::TextureHandle handle) {
    return static_cast<VulkanTexture*>(get_tex(handle));
  }
  [[nodiscard]] VkImageView get_vk_tex_view(rhi::TextureHandle handle, int subresource_id);
  VulkanBuffer* get_vk_buf(rhi::BufferHandle handle) {
    return static_cast<VulkanBuffer*>(get_buf(handle));
  }

 private:
  friend class VulkanCmdEncoder;
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
  VkFence frame_fences_[(int)rhi::QueueType::Count][k_max_frames_in_flight]{};

  Queue queues_[(int)rhi::QueueType::Count]{};
  gch::small_vector<uint32_t, 4> queue_family_indices_;

  std::vector<std::unique_ptr<VulkanCmdEncoder>> cmd_encoders_;
  size_t curr_cmd_encoder_i_{};
  // Per in-flight frame slot: push constant blobs from prepare_indexed_indirect_draws, keyed by
  // returned id. Cleared when the first encoder of a submission batch begins (see
  // begin_cmd_encoder).
  struct IndexedIndirectPCSlots {
    struct Slot {
      std::vector<uint8_t> pc;
      rhi::BufferHandle index_buf;
      size_t index_buf_offset;
    };
    std::vector<Slot> slots;
  };

  IndexedIndirectPCSlots indexed_indirect_pc_cache_[k_max_frames_in_flight]{};
  VmaAllocator allocator_;
  size_t frame_num_{};
  std::filesystem::path shader_lib_dir_;

 public:
  // TODO: lmao
  std::unordered_map<uint64_t, rhi::PipelineHandle> all_pipelines_;

 private:
  VkShaderModule create_shader_module(const std::filesystem::path& path);
  VkShaderModule create_shader_module(std::span<const uint32_t> spirv_code);
  VkSampler create_vk_sampler(const rhi::SamplerDesc& desc);
  struct CachedPipelineLayout {
    VkPipelineLayout layout{};
    VkDescriptorSetLayout set0_layout{};
    uint32_t bindless_first_set{0};
    std::vector<VkDescriptorSet> bindless_sets;
  };
  std::unordered_map<uint64_t, VkDescriptorSetLayout> set_layout_cache_;
  std::unordered_map<uint64_t, CachedPipelineLayout> pipeline_layout_cache_;
  uint64_t hash_descriptor_set_layout_cinfo(const VkDescriptorSetLayoutCreateInfo& cinfo);
  uint64_t hash_pipeline_layout_cinfo(const VkPipelineLayoutCreateInfo& cinfo);

  struct DescSetCreateInfo {
    std::vector<VkDescriptorSetLayoutBinding> bindings;
  };
  struct BindlessBindingUsage {
    bool used = false;
    VkDescriptorSetLayoutBinding binding{};
  };

  void reflect_shader(std::span<const uint32_t> spirv_code, VkShaderStageFlagBits stage,
                      std::vector<VkPushConstantRange>& out_pc_ranges,
                      DescSetCreateInfo& out_set_0_info,
                      std::vector<BindlessBindingUsage>& out_shader_bindless);
  static void merge_bindless_reflection(std::vector<BindlessBindingUsage>& dst,
                                        const std::vector<BindlessBindingUsage>& src);
  CachedPipelineLayout get_or_create_pipeline_layout(
      const std::vector<VkDescriptorSetLayoutBinding>& merged_set0,
      const std::vector<BindlessBindingUsage>& merged_bindless, VkPushConstantRange* pc_ranges,
      uint32_t pc_range_count);

  struct BindlessHeap {
    VkDescriptorPool pool{};
    VkDescriptorSetLayout layout{};
    VkDescriptorSet set{};
    std::mutex mutex;
    std::vector<uint32_t> freelist;
    uint32_t capacity{0};
  };

  void init_bindless_heaps();
  void shutdown_bindless_heaps();
  void init_uab_heap(BindlessHeap& heap, VkDescriptorType type, uint32_t count);
  void shutdown_uab_heap(BindlessHeap& heap);
  static int alloc_uab_heap_slot(BindlessHeap& heap);
  static void free_uab_heap_slot(BindlessHeap& heap, uint32_t idx);

  int alloc_bindless_storage_idx();
  void free_bindless_storage_idx(uint32_t idx);
  void write_bindless_storage_descriptor(uint32_t idx, VkBuffer buffer);

  int alloc_bindless_image_slot();
  void free_bindless_image_slot(uint32_t idx);
  void write_bindless_sampled_image(uint32_t idx, VkImageView view, VkImageLayout layout);
  void write_bindless_storage_image(uint32_t idx, VkImageView view, VkImageLayout layout);
  void clear_bindless_image_slot(uint32_t idx);
  void write_bindless_uniform_texel(uint32_t idx, VkBufferView view);
  void write_bindless_storage_texel(uint32_t idx, VkBufferView view);

  int alloc_bindless_sampler_idx();
  void free_bindless_sampler_idx(uint32_t idx);
  void write_bindless_sampler(uint32_t idx, VkSampler sampler);

  gch::small_vector<VkSampler, 10> immutable_samplers_;

  VkBuffer null_storage_buffer_{};
  VmaAllocation null_storage_buffer_alloc_{};
  VkDescriptorPool bindless_storage_pool_{};
  VkDescriptorSetLayout bindless_storage_layout_{};
  VkDescriptorSet bindless_storage_set_{};
  std::mutex bindless_storage_mutex_;
  std::vector<uint32_t> bindless_storage_freelist_;
  uint32_t bindless_storage_capacity_{0};

  VkBuffer null_texel_buffer_{};
  VmaAllocation null_texel_buffer_alloc_{};
  VkBufferView null_uniform_texel_view_{};
  VkBufferView null_storage_texel_view_{};

  VkImage null_image_{};
  VmaAllocation null_image_alloc_{};
  VkImageView null_image_view_{};

  VkSampler null_bindless_sampler_{};

  BindlessHeap bindless_uniform_texel_{};
  BindlessHeap bindless_sampler_{};
  BindlessHeap bindless_sampled_image_{};
  BindlessHeap bindless_storage_image_{};
  BindlessHeap bindless_storage_texel_{};

  VkDescriptorPool padding_descriptor_pool_{};
  VkDescriptorSetLayout empty_descriptor_set_layout_{};
  VkDescriptorSet empty_descriptor_set_{};
};

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
