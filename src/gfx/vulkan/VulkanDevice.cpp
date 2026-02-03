#include "VulkanDevice.hpp"

// clang-format off
#include <volk.h>
#include <VkBootstrap.h>
#include <fstream>
// clang-format on

#include "VMAWrapper.hpp"  // IWYU pragma: keep
#include "VulkanDeleteQueue.hpp"
#include "Window.hpp"
#include "core/Config.hpp"
#include "core/EAssert.hpp"
#include "core/Hash.hpp"
#include "core/Logger.hpp"
#include "core/Util.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "gfx/vulkan/VkUtil.hpp"
#include "gfx/vulkan/VulkanCommon.hpp"
#include "spirv_reflect.h"
#include "vulkan/vk_enum_string_helper.h"
#include "vulkan/vulkan_core.h"

namespace TENG_NAMESPACE {

namespace gfx::vk {

namespace {

VkShaderStageFlagBits convert_shader_stage(rhi::ShaderType type) {
  switch (type) {
    case rhi::ShaderType::Vertex:
      return VK_SHADER_STAGE_VERTEX_BIT;
    case rhi::ShaderType::Fragment:
      return VK_SHADER_STAGE_FRAGMENT_BIT;
    case rhi::ShaderType::Compute:
      return VK_SHADER_STAGE_COMPUTE_BIT;
    case rhi::ShaderType::Mesh:
      return VK_SHADER_STAGE_MESH_BIT_EXT;
    case rhi::ShaderType::Task:
      return VK_SHADER_STAGE_TASK_BIT_EXT;
    default:
      return VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
  }
}
std::vector<uint32_t> read_file_to_uint_vec(const std::filesystem::path& path) {
  std::vector<uint32_t> result;

  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    LERROR("Error reading file: {}", path.c_str());
    return result;
  }

  size_t size_bytes = file.tellg();
  file.seekg(std::ios::beg);

  result.resize(size_bytes / sizeof(uint32_t));
  file.read(reinterpret_cast<char*>(result.data()), size_bytes);
  return result;
}

using rhi::PolygonMode;
using rhi::TextureFormat;

constexpr VkFormat convert_format(TextureFormat format) {
  switch (format) {
    case TextureFormat::R8G8B8A8Srgb:
      return VK_FORMAT_R8G8B8A8_SRGB;
    case TextureFormat::R8G8B8A8Unorm:
      return VK_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::B8G8R8A8Unorm:
      return VK_FORMAT_B8G8R8A8_UNORM;
    case TextureFormat::B8G8R8A8Srgb:
      return VK_FORMAT_B8G8R8A8_SRGB;
    case TextureFormat::D32float:
      return VK_FORMAT_D32_SFLOAT;
    case TextureFormat::R32float:
      return VK_FORMAT_R32_SFLOAT;
    case TextureFormat::Undefined:
    default:
      ASSERT(0);
      return VK_FORMAT_UNDEFINED;
  }
}
constexpr TextureFormat convert_format(VkFormat format) {
  switch (format) {
    case VK_FORMAT_R8G8B8A8_SRGB:
      return TextureFormat::R8G8B8A8Srgb;
    case VK_FORMAT_R8G8B8A8_UNORM:
      return TextureFormat::R8G8B8A8Unorm;
    case VK_FORMAT_B8G8R8A8_UNORM:
      return TextureFormat::B8G8R8A8Unorm;
    case VK_FORMAT_B8G8R8A8_SRGB:
      return TextureFormat::B8G8R8A8Srgb;
    case VK_FORMAT_D32_SFLOAT:
      return TextureFormat::D32float;
    case VK_FORMAT_R32_SFLOAT:
      return TextureFormat::R32float;
    default:
      ASSERT(0);
      return TextureFormat::Undefined;
  }
}

VkPolygonMode convert_polygon_mode(PolygonMode mode) {
  switch (mode) {
    case PolygonMode::Fill:
      return VK_POLYGON_MODE_FILL;
    case PolygonMode::Point:
      return VK_POLYGON_MODE_POINT;
    case PolygonMode::Line:
      return VK_POLYGON_MODE_LINE;
    default:
      return VK_POLYGON_MODE_MAX_ENUM;
  }
}

constexpr VkStencilOpState convert_stencil_op_state(
    const rhi::GraphicsPipelineCreateInfo::StencilOpState& state) {
  return VkStencilOpState{.failOp = static_cast<VkStencilOp>(state.fail_op),
                          .passOp = static_cast<VkStencilOp>(state.pass_op),
                          .depthFailOp = static_cast<VkStencilOp>(state.depth_fail_op),
                          .compareOp = static_cast<VkCompareOp>(state.compare_op),
                          .compareMask = state.compare_mask,
                          .writeMask = state.write_mask,
                          .reference = state.reference};
}
constexpr VkLogicOp convert_logic_op(rhi::LogicOp op) { return static_cast<VkLogicOp>(op); }

constexpr VkColorComponentFlags convert_color_component_flags(rhi::ColorComponentFlags flags) {
  return flags;
}

constexpr VkBlendOp convert_blend_op(rhi::BlendOp op) { return static_cast<VkBlendOp>(op); }

constexpr VkBlendFactor convert_blend_factor(rhi::BlendFactor factor) {
  return static_cast<VkBlendFactor>(factor);
}

constexpr VkPipelineColorBlendAttachmentState convert_color_blend_attachment(
    const rhi::GraphicsPipelineCreateInfo::ColorBlendAttachment& a) {
  return VkPipelineColorBlendAttachmentState{
      .blendEnable = a.enable,
      .srcColorBlendFactor = convert_blend_factor(a.src_color_factor),
      .dstColorBlendFactor = convert_blend_factor(a.dst_color_factor),
      .colorBlendOp = convert_blend_op(a.color_blend_op),
      .srcAlphaBlendFactor = convert_blend_factor(a.src_alpha_factor),
      .dstAlphaBlendFactor = convert_blend_factor(a.dst_alpha_factor),
      .alphaBlendOp = convert_blend_op(a.alpha_blend_op),
      .colorWriteMask = convert_color_component_flags(a.color_write_mask),
  };
}

VKAPI_ATTR VkBool32 VKAPI_CALL
vk_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                  VkDebugUtilsMessageTypeFlagsEXT messageType,
                  const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void*) {
  const char* ms = vkb::to_string_message_severity(messageSeverity);
  const char* mt = vkb::to_string_message_type(messageType);
  if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
    std::println("[{}: {}] - {}\n{}", ms, mt, pCallbackData->pMessageIdName,
                 pCallbackData->pMessage);
  } else {
    std::println("[{}: {}]\n{}", ms, mt, pCallbackData->pMessage);
  }

  ALWAYS_ASSERT(0);
  return VK_FALSE;
}

void check_vkb_result(const char* err_msg, const auto& vkb_result) {
  if (!vkb_result) {
    LCRITICAL("{}: {}", err_msg, vkb_result.error().message());
    exit(1);
  }
}

}  // namespace

void VulkanDevice::shutdown() {
  vkDeviceWaitIdle(device_);

  del_q_.flush(SIZE_T_MAX);

  for (auto& cmd : cmd_encoders_) {
    for (uint32_t pool_i = 0; pool_i < frames_in_flight(); pool_i++) {
      cmd->binder_pools_[pool_i].destroy(*this);
    }
  }

  // TODO: remove
  vkDestroyPipelineLayout(device_, default_pipeline_layout_, nullptr);
  for (auto& [hash, layout] : set_layout_cache_) {
    vkDestroyDescriptorSetLayout(device_, layout, nullptr);
  }

  for (auto& frame_fence : frame_fences_) {
    for (size_t frame_i = 0; frame_i < info_.frames_in_flight; frame_i++) {
      vkDestroyFence(device_, frame_fence[frame_i], nullptr);
    }
  }

  for (size_t i = 0; i < info_.frames_in_flight; i++) {
    vkDestroyCommandPool(device_, command_pools_[i], nullptr);
  }
  vmaDestroyAllocator(allocator_);
  vkDestroyDevice(device_, nullptr);
  vkb::destroy_instance(vkb_inst_);
}

void VulkanDevice::init(const InitInfo& init_info) {
  info_.frames_in_flight = init_info.frames_in_flight;
  shader_lib_dir_ = init_info.shader_lib_dir;
  shader_lib_dir_ /= "metal";
  vkb::InstanceBuilder builder{};

  VK_CHECK(volkInitialize());

  if (init_info.app_name.size()) {
    builder.set_app_name(init_info.app_name.c_str());
  }

  constexpr int min_api_version_major = 1;
  constexpr int min_api_version_minor = 3;

  auto inst_ret = builder.request_validation_layers(init_info.validation_layers_enabled)
                      .set_minimum_instance_version(min_api_version_major, min_api_version_minor, 0)
                      .require_api_version(min_api_version_major, min_api_version_minor, 0)
                      .set_debug_callback(vk_debug_callback)
                      .build();
  check_vkb_result("Failed to get vulkan instance", inst_ret);
  vkb_inst_ = inst_ret.value();
  instance_ = vkb_inst_.instance;

  volkLoadInstance(instance_);

  vkb::PhysicalDeviceSelector phys_device_selector{vkb_inst_};
  const char* required_extensions[] = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME,
      VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
      VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
  };
  phys_device_selector.add_required_extensions(ARRAY_SIZE(required_extensions),
                                               required_extensions);
  VkPhysicalDeviceVulkan13Features feat13{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .synchronization2 = true,
      .dynamicRendering = true,
  };
  phys_device_selector.set_required_features_13(feat13);

  auto phys_ret = phys_device_selector.defer_surface_initialization()
                      .set_minimum_version(min_api_version_major, min_api_version_minor)
                      .select();
  check_vkb_result("Failed to select physical device", phys_ret);
  physical_device_ = phys_ret.value().physical_device;

  vkb::DeviceBuilder device_builder{phys_ret.value()};
  auto device_ret = device_builder.build();
  check_vkb_result("Failed to create vulkan device", device_ret);
  device_ = device_ret.value().device;

  vkb_device_ = device_ret.value();

  bool found_graphics_queue{};
  for (uint32_t i = 0; i < static_cast<uint32_t>(vkb_device_.queue_families.size()); i++) {
    const auto& queue_family = vkb_device_.queue_families[i];
    if (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      vkGetDeviceQueue(device_, i, 0, &queues_[(int)rhi::QueueType::Graphics].queue);
      found_graphics_queue = true;
      break;
    }
  }

  if (!found_graphics_queue) {
    LCRITICAL("Failed to find graphics queue");
    exit(1);
  }

  for (auto& frame_fence : frame_fences_) {
    for (size_t frame_i = 0; frame_i < info_.frames_in_flight; frame_i++) {
      VkFenceCreateInfo fence_cinfo{
          .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      };
      VK_CHECK(vkCreateFence(device_, &fence_cinfo, nullptr, &frame_fence[frame_i]));
    }
  }

  VmaVulkanFunctions vma_funcs{};
  vma_funcs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
  vma_funcs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
  vma_funcs.vkAllocateMemory = vkAllocateMemory;
  vma_funcs.vkBindBufferMemory = vkBindBufferMemory;
  vma_funcs.vkBindImageMemory = vkBindImageMemory;
  vma_funcs.vkCreateBuffer = vkCreateBuffer;
  vma_funcs.vkCreateImage = vkCreateImage;
  vma_funcs.vkDestroyBuffer = vkDestroyBuffer;
  vma_funcs.vkDestroyImage = vkDestroyImage;
  vma_funcs.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
  vma_funcs.vkFreeMemory = vkFreeMemory;
  vma_funcs.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
  vma_funcs.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
  vma_funcs.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
  vma_funcs.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2KHR;
  vma_funcs.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
  vma_funcs.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
  vma_funcs.vkMapMemory = vkMapMemory;
  vma_funcs.vkUnmapMemory = vkUnmapMemory;
  vma_funcs.vkCmdCopyBuffer = vkCmdCopyBuffer;
  VmaAllocatorCreateInfo allocator_info{
      .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
      .physicalDevice = physical_device_,
      .device = device_,
      .pVulkanFunctions = &vma_funcs,
      .instance = instance_,
  };

  VK_CHECK(vmaCreateAllocator(&allocator_info, &allocator_));

  volkLoadDevice(device_);

  for (size_t i = 0; i < info_.frames_in_flight; i++) {
    VkCommandPoolCreateInfo cinfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queues_[(int)rhi::QueueType::Graphics].family_idx,
    };
    VK_CHECK(vkCreateCommandPool(device_, &cinfo, nullptr, &command_pools_[i]));
  }

  // NEED: VK_EXT_mutable_descriptor_type
  // {
  //   VkDescriptorPoolSize size{.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR};
  //   // descriptor pool
  //   VkDescriptorPoolCreateInfo pool_cinfo{
  //       .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
  //       .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
  //   };
  // }

  // {  // default pipeline layout
  //   VkPushConstantRange pc_range{.stageFlags = VK_SHADER_STAGE_ALL, .offset = 0, .size = 120};
  //   VkDescriptorSetLayoutBinding b1{
  //       .binding = 0,
  //       .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
  //       .descriptorCount = 3,
  //   };
  //   VkDescriptorSetLayoutBinding b2{
  //       .binding = 1,
  //       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
  //       .descriptorCount = 9,
  //   };
  //   VkDescriptorSetLayoutCreateInfo layout_cinfo{
  //       .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
  //       .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
  //   };
  //   VkDescriptorSetLayout main_desc_set_layout{
  //
  //   };
  //   VkPipelineLayoutCreateInfo default_layout_cinfo{
  //       .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
  //       .setLayoutCount = 1,
  //       .pSetLayouts = &main_desc_set_layout,
  //       .pushConstantRangeCount = 1,
  //       .pPushConstantRanges = &pc_range,
  //   };
  //   vkCreatePipelineLayout(device_, &default_layout_cinfo, nullptr, &default_pipeline_layout_);
  // }
  del_q_.init(device_, info_.frames_in_flight);
}

rhi::BufferHandle VulkanDevice::create_buf(const rhi::BufferDesc& desc) {
  ALWAYS_ASSERT(desc.usage);
  VkBufferCreateInfo cinfo{
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = desc.size,
      .sharingMode = VK_SHARING_MODE_CONCURRENT,
  };
  if (desc.usage & rhi::BufferUsage_Index) {
    cinfo.usage |= VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT;
  }
  if (desc.usage & rhi::BufferUsage_Indirect) {
    cinfo.usage |= VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT;
  }
  if (desc.usage & rhi::BufferUsage_Storage) {
    cinfo.usage |= VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT;
  }
  if (desc.usage & rhi::BufferUsage_Uniform) {
    cinfo.usage |= VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT;
  }
  if (desc.usage & rhi::BufferUsage_Vertex) {
    cinfo.usage |= VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT;
  }

  // cinfo.usage |= VK_BUFFER_USAGE_2_MICROMAP_STORAGE_BIT_EXT
  VmaAllocationCreateInfo vma_cinfo{};
  if (desc.storage_mode == rhi::StorageMode::CPUAndGPU) {
    vma_cinfo.flags |=
        VMA_ALLOCATION_CREATE_MAPPED_BIT |
        (desc.random_host_access ? VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
                                 : VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
  } else {
    cinfo.usage |= VK_BUFFER_USAGE_2_TRANSFER_DST_BIT | VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT;
  }

  vma_cinfo.usage = VMA_MEMORY_USAGE_AUTO;
  VkBuffer buffer{};
  VmaAllocation allocation{};
  VK_CHECK(vmaCreateBuffer(allocator_, &cinfo, &vma_cinfo, &buffer, &allocation, nullptr));

  return buffer_pool_.alloc(desc, rhi::k_invalid_bindless_idx, buffer, allocation);
}

rhi::TextureHandle VulkanDevice::create_tex(const rhi::TextureDesc& desc) {
  VkImageCreateInfo cinfo{.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  cinfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  if (desc.usage & rhi::TextureUsageSample) {
    cinfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
  }
  if (desc.usage & rhi::TextureUsageStorage) {
    cinfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
  }
  if (desc.usage & rhi::TextureUsageColorAttachment) {
    cinfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  }
  if (desc.usage & rhi::TextureUsageDepthStencilAttachment) {
    cinfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  }

  VmaAllocationCreateInfo alloc_info{};
  alloc_info.usage = VMA_MEMORY_USAGE_AUTO;

  VkImage image;
  VmaAllocation allocation;
  VmaAllocationInfo allocation_info;

  VkImageViewType view_type;
  if (desc.dims.z > 1) {
    view_type = VK_IMAGE_VIEW_TYPE_3D;
  } else if (desc.dims.y > 1) {
    view_type = desc.array_length > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
  } else {
    view_type = desc.array_length > 1 ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
  }

  VK_CHECK(vmaCreateImage(allocator_, &cinfo, &alloc_info, &image, &allocation, nullptr));
  auto handle = texture_pool_.alloc(desc, rhi::k_invalid_bindless_idx, image, allocation,
                                    allocation_info, false);

  auto* tex = (VulkanTexture*)get_tex(handle);
  tex->default_view_ = create_img_view(*tex, view_type,
                                       {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                        .baseMipLevel = 0,
                                        .levelCount = desc.mip_levels,
                                        .baseArrayLayer = 0,
                                        .layerCount = desc.array_length});
  return handle;
}

rhi::CmdEncoder* VulkanDevice::begin_cmd_encoder() {
  if (curr_cmd_encoder_i_ >= cmd_encoders_.size()) {
    // TODO: pipeline layout
    auto& enc = cmd_encoders_.emplace_back(std::make_unique<VulkanCmdEncoder>(this));

    for (size_t i = 0; i < frames_in_flight(); i++) {
      VkCommandBufferAllocateInfo info{
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
          .commandPool = command_pools_[i],
          .commandBufferCount = 1,
      };
      VK_CHECK(vkAllocateCommandBuffers(device_, &info, &enc->cmd_bufs_[i]));
    }
  }

  auto& enc = *cmd_encoders_[curr_cmd_encoder_i_];
  enc.curr_frame_i_ = frame_idx();
  enc.submit_swapchains_.clear();
  enc.queue_type_ = rhi::QueueType::Graphics;

  if (!enc.binder_pools_[enc.curr_frame_i_].pool) {
    for (size_t i = 0; i < frames_in_flight(); i++) {
      enc.binder_pools_[i].init(*this);
    }
    auto& binder = enc.binder_;
    binder.writes.reserve(120);
    binder.img_infos.reserve(60);
  } else {
    enc.binder_pools_[enc.curr_frame_i_].reset(*this);
  }

  VkCommandBufferBeginInfo begin_info{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
  VK_CHECK(vkResetCommandBuffer(enc.cmd_bufs_[frame_idx()], 0));
  VK_CHECK(vkBeginCommandBuffer(enc.cmd_bufs_[frame_idx()], &begin_info));
  curr_cmd_encoder_i_++;
  return &enc;
}

rhi::PipelineHandle VulkanDevice::create_graphics_pipeline(
    const rhi::GraphicsPipelineCreateInfo& info) {
  std::array<VkPipelineColorBlendAttachmentState, 10> attachments{};
  uint32_t i = 0;
  uint32_t attachment_cnt = info.blend.attachments.size();
  for (const auto& attachment : info.blend.attachments) {
    attachments[i++] = convert_color_blend_attachment(attachment);
  }
  VkPipelineColorBlendStateCreateInfo blend_state{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable = info.blend.logic_op_enable,
      .logicOp = convert_logic_op(info.blend.logic_op),
      .attachmentCount = attachment_cnt,
      .pAttachments = attachments.data()};

  VkPipelineMultisampleStateCreateInfo multisample{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples =
          static_cast<VkSampleCountFlagBits>(info.multisample.rasterization_samples),
      .sampleShadingEnable = info.multisample.sample_shading_enable,
      .minSampleShading = info.multisample.min_sample_shading,
      .alphaToCoverageEnable = info.multisample.alpha_to_coverage_enable,
      .alphaToOneEnable = info.multisample.alpha_to_one_enable};
  VkPipelineDepthStencilStateCreateInfo depth_stencil{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = info.depth_stencil.depth_test_enable,
      .depthWriteEnable = info.depth_stencil.depth_write_enable,
      .depthCompareOp = static_cast<VkCompareOp>(info.depth_stencil.depth_compare_op),
      .depthBoundsTestEnable = info.depth_stencil.depth_bounds_test_enable,
      .stencilTestEnable = info.depth_stencil.stencil_test_enable,
      .front = convert_stencil_op_state(info.depth_stencil.stencil_front),
      .back = convert_stencil_op_state(info.depth_stencil.stencil_back),
      .minDepthBounds = info.depth_stencil.min_depth_bounds,
      .maxDepthBounds = info.depth_stencil.max_depth_bounds};

  VkPipelineViewportStateCreateInfo viewport_state{};
  viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.pNext = nullptr;
  viewport_state.viewportCount = 1;
  viewport_state.scissorCount = 1;

  // TODO: configurable dynamic state
  VkPipelineDynamicStateCreateInfo dynamic_state{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};

  VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
                             VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_FRONT_FACE,
                             VK_DYNAMIC_STATE_CULL_MODE};
  dynamic_state.dynamicStateCount = ARRAY_SIZE(states);
  dynamic_state.pDynamicStates = states;

  VkPipelineVertexInputStateCreateInfo vertex_state{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo input_assembly{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};

  std::array<VkFormat, 5> color_formats;
  uint32_t color_format_cnt = 0;
  for (auto format : info.rendering.color_formats) {
    if (format != rhi::TextureFormat::Undefined) {
      color_formats[color_format_cnt] = convert_format(format);
      color_format_cnt++;
    } else {
      break;
    }
  }
  // dummy blend attachment if color attachment is specified but no blending
  if (i == 0 && color_format_cnt > 0) {
    attachment_cnt = color_format_cnt;
    auto default_blend =
        convert_color_blend_attachment(rhi::GraphicsPipelineCreateInfo::ColorBlendAttachment{});
    for (uint32_t i = 0; i < attachment_cnt; i++) {
      attachments[i] = default_blend;
    }
  }
  VkPipelineRenderingCreateInfo rendering_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = color_format_cnt,
      .pColorAttachmentFormats = color_formats.data(),
      .depthAttachmentFormat = convert_format(info.rendering.depth_format),
      .stencilAttachmentFormat = convert_format(info.rendering.stencil_format)};

  VkPipelineRasterizationStateCreateInfo rasterization{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable = info.rasterization.depth_clamp,
      .rasterizerDiscardEnable = info.rasterization.rasterize_discard_enable,
      .polygonMode = convert_polygon_mode(info.rasterization.polygon_mode),
      .depthBiasEnable = info.rasterization.depth_bias,
      .depthBiasConstantFactor = info.rasterization.depth_bias_constant_factor,
      .depthBiasClamp = info.rasterization.depth_bias_clamp,
      .depthBiasSlopeFactor = info.rasterization.depth_bias_slope_factor,
      .lineWidth = info.rasterization.line_width};

  std::array<VkPipelineShaderStageCreateInfo, 2> stages;
  uint32_t module_count = 0;
  for (size_t i = 0; i < info.shaders.size(); i++, module_count++) {
    const auto& shader_info = info.shaders[i];
    if (shader_info.type == rhi::ShaderType::None) {
      break;
    }

    VkShaderModule module = create_shader_module(shader_info.path);
    stages[i] = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = convert_shader_stage(shader_info.type),
        .module = module,
        .pName = info.shaders[i].entry_point.c_str(),
    };
  }

  VkGraphicsPipelineCreateInfo cinfo{
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &rendering_info,
      .stageCount = module_count,
      .pStages = stages.data(),
      .pVertexInputState = &vertex_state,
      .pInputAssemblyState = &input_assembly,  // dynamic
      .pTessellationState = nullptr,
      .pViewportState = &viewport_state,
      .pRasterizationState = &rasterization,
      .pMultisampleState = &multisample,
      .pDepthStencilState = &depth_stencil,
      .pColorBlendState = &blend_state,
      .pDynamicState = &dynamic_state,
      .layout = default_pipeline_layout_,
  };

  VkPipeline vk_pipeline;
  VK_CHECK(vkCreateGraphicsPipelines(device_, nullptr, 1, &cinfo, nullptr, &vk_pipeline));

  for (uint32_t i = 0; i < module_count; i++) {
    vkDestroyShaderModule(device_, stages[i].module, nullptr);
  }
  ASSERT(0 && " unhandled pipeline layout and descriptor set layout");
  std::vector<VkDescriptorSetLayoutBinding> bindings;
  return pipeline_pool_.alloc(info, vk_pipeline, nullptr, nullptr, std::move(bindings));
}

void VulkanDevice::submit_frame() {
  // submit queues
  for (size_t cmd_enc_i = 0; cmd_enc_i < curr_cmd_encoder_i_; cmd_enc_i++) {
    auto& enc = *cmd_encoders_[cmd_enc_i];
    auto& queue = queues_[(int)enc.queue_type_];
    ASSERT(queue.is_valid());

    queue.submit_cmd_bufs.emplace_back(VkCommandBufferSubmitInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = enc.cmd_bufs_[frame_idx()],
    });

    // wait for swapchains to be ready
    for (auto& rhi_swapchain : enc.submit_swapchains_) {
      auto& swapchain = *(VulkanSwapchain*)rhi_swapchain;
      queue.wait_semaphores.emplace_back(VkSemaphoreSubmitInfo{
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
          .semaphore = swapchain.acquire_semaphores_[swapchain.acquire_semaphore_idx_],
          .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      });
      queue.signal_semaphores.emplace_back(VkSemaphoreSubmitInfo{
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
          .semaphore = swapchain.ready_to_present_semaphores_[swapchain.curr_img_idx_],
          .stageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
      });
      queue.present_swapchains.emplace_back(swapchain.swapchain_);
      queue.present_swapchain_img_indices.emplace_back(swapchain.curr_img_idx_);
      queue.present_wait_semaphores.emplace_back(
          swapchain.ready_to_present_semaphores_[swapchain.curr_img_idx_]);
    }
  }

  for (size_t queue_t = 0; queue_t < (size_t)rhi::QueueType::Count; queue_t++) {
    auto& queue = queues_[queue_t];
    if (queue.is_valid()) {
      queue.submit(frame_fences_[queue_t][frame_idx()]);
    }
  }

  // presents

  frame_num_++;

  // wait for fences that frame number N - (frames_in_flight) signaled
  // nothing to wait for during the first 2-3 frames
  if (frame_num_ >= frames_in_flight()) {
    VkFence wait_fences[(int)rhi::QueueType::Count];
    VkFence reset_fences[(int)rhi::QueueType::Count];
    uint32_t wait_fence_count = 0, reset_fence_count = 0;
    for (size_t queue_t = 0; queue_t < (int)rhi::QueueType::Count; queue_t++) {
      auto& queue = queues_[queue_t];
      if (!queue.is_valid()) {
        continue;
      }
      VkFence fence = frame_fences_[queue_t][frame_idx()];
      reset_fences[reset_fence_count++] = fence;
      auto status = vkGetFenceStatus(device_, fence);
      if (status != VK_SUCCESS) {
        ASSERT(status != VK_ERROR_DEVICE_LOST);
        wait_fences[wait_fence_count++] = fence;
      }
    }
    if (wait_fence_count > 0) {
      VK_CHECK(vkWaitForFences(device_, wait_fence_count, wait_fences, VK_TRUE, UINT64_MAX));
    }
    VK_CHECK(vkResetFences(device_, reset_fence_count, reset_fences));
  }
  curr_cmd_encoder_i_ = 0;
  del_q_.flush(frame_num_);
  del_q_.set_curr_frame(frame_num_);
}

rhi::SwapchainHandle VulkanDevice::create_swapchain(const rhi::SwapchainDesc& desc) {
  VkSurfaceKHR surface{};
  ASSERT(instance_);
  VK_CHECK(glfwCreateWindowSurface(instance_, desc.window->get_handle(), nullptr, &surface));
  ASSERT(surface);
  auto handle = swapchain_pool_.alloc(surface);
  auto* swapchain = (VulkanSwapchain*)get_swapchain(handle);
  recreate_swapchain(desc, swapchain);

  return handle;
}

void VulkanDevice::destroy(rhi::BufferHandle handle) {
  auto* buf = (VulkanBuffer*)get_buf(handle);
  if (buf) {
    vmaDestroyBuffer(allocator_, buf->buffer_, buf->allocation_);
    buffer_pool_.destroy(handle);
  }
}

void VulkanDevice::destroy(rhi::PipelineHandle handle) {
  auto* pipeline = (VulkanPipeline*)get_pipeline(handle);
  if (pipeline) {
    del_q_.enqueue(pipeline->pipeline_);
    pipeline_pool_.destroy(handle);
  }
}

void VulkanDevice::destroy(rhi::TextureHandle handle) {
  auto* tex = (VulkanTexture*)get_tex(handle);
  if (tex) {
    if (!tex->is_swapchain_image_) {
      vmaDestroyImage(allocator_, tex->image_, tex->allocation_);
    }
    if (tex->default_view_) {
      del_q_.enqueue(tex->default_view_);
    }
    texture_pool_.destroy(handle);
  }
}
void VulkanDevice::destroy(rhi::SamplerHandle handle) {
  auto* sampler = (VulkanSampler*)sampler_pool_.get(handle);
  if (sampler) {
    ASSERT(0);
    // vkDestroySampler(device_, sampler->sampler_, nullptr);
  }

  sampler_pool_.destroy(handle);
}

VkImageView VulkanDevice::create_img_view(VulkanTexture& img, VkImageViewType type,
                                          const VkImageSubresourceRange& subresource_range) {
  VkImageViewCreateInfo cinfo;
  cinfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  cinfo.pNext = nullptr;
  cinfo.flags = 0;
  cinfo.image = img.image_;
  cinfo.viewType = type;

  cinfo.format = convert_format(img.desc().format);
  cinfo.components = {.r = VK_COMPONENT_SWIZZLE_IDENTITY,
                      .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                      .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                      .a = VK_COMPONENT_SWIZZLE_IDENTITY};
  cinfo.subresourceRange = subresource_range;
  VkImageView view;
  VK_CHECK(vkCreateImageView(device_, &cinfo, nullptr, &view));
  return view;
}

void VulkanDevice::begin_swapchain_rendering(rhi::Swapchain* swapchain, rhi::CmdEncoder* cmd_enc,
                                             glm::vec4* clear_color) {
  // acquire next swapchain image
  auto* swap = (VulkanSwapchain*)swapchain;
  swap->acquire_semaphore_idx_ = (swap->acquire_semaphore_idx_ + 1) % swap->swapchain_tex_count_;

  // TODO: while loop
  constexpr int k_timeout_value = 1'000'000'000;
  ASSERT(swap->acquire_semaphores_[swap->acquire_semaphore_idx_]);

  VkResult acquire_result;
  do {
    acquire_result = vkAcquireNextImageKHR(device_, swap->swapchain_, k_timeout_value,
                                           swap->acquire_semaphores_[swap->acquire_semaphore_idx_],
                                           nullptr, &swap->curr_img_idx_);
    if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR || acquire_result == VK_SUBOPTIMAL_KHR) {
      recreate_swapchain(swapchain->desc_, swapchain);
    }
  } while (acquire_result == VK_TIMEOUT || acquire_result == VK_ERROR_OUT_OF_DATE_KHR ||
           acquire_result == VK_SUBOPTIMAL_KHR);
  if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR &&
      acquire_result != VK_ERROR_OUT_OF_DATE_KHR) {
    LCRITICAL("Failed to acquire swapchain image!");
    ALWAYS_ASSERT(0);
  }

  auto* vk_enc = (VulkanCmdEncoder*)cmd_enc;
  vk_enc->submit_swapchains_.emplace_back(swapchain);

  VkImage curr_image =
      ((VulkanTexture*)get_tex(swapchain->get_texture(swap->curr_img_idx_)))->image();
  VkImageMemoryBarrier2 img_barriers[] = {
      VkImageMemoryBarrier2{
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
          .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
          .srcAccessMask = 0,
          .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
          .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
          .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          .image = curr_image,
          .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .levelCount = 1,
                               .layerCount = 1},
      },
  };
  VkDependencyInfo info{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                        .imageMemoryBarrierCount = ARRAY_SIZE(img_barriers),
                        .pImageMemoryBarriers = img_barriers};
  vkCmdPipelineBarrier2KHR(vk_enc->cmd(), &info);

  // vk_enc->render_pass_end_img_barriers_.emplace_back(VkImageMemoryBarrier2{
  //     .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
  //     .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
  //     .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
  //     .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
  //     .dstAccessMask = 0,
  //     .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  //     .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
  //     .image = curr_image,
  //     .subresourceRange = {
  //         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}});

  cmd_enc->begin_rendering({
      rhi::RenderingAttachmentInfo{
          .image = swapchain->get_texture(swap->curr_img_idx_),
          .load_op = clear_color ? rhi::LoadOp::Clear : rhi::LoadOp::DontCare,
          .store_op = rhi::StoreOp::Store,
          .clear_value = clear_color ? rhi::ClearValue{.color = *clear_color} : rhi::ClearValue{},
      },
  });
}

void VulkanDevice::destroy(rhi::SwapchainHandle handle) {
  auto* swapchain = static_cast<VulkanSwapchain*>(get_swapchain(handle));
  if (swapchain) {
    for (VkSemaphore sem : swapchain->acquire_semaphores_) {
      del_q_.enqueue(sem);
    }
    for (VkSemaphore sem : swapchain->ready_to_present_semaphores_) {
      del_q_.enqueue(sem);
    }
    for (size_t i = 0; i < swapchain->swapchain_tex_count_; i++) {
      destroy(swapchain->textures_[i]);
    }
    vkDestroySwapchainKHR(device_, swapchain->swapchain_, nullptr);
    vkDestroySurfaceKHR(instance_, swapchain->surface_, nullptr);
    swapchain_pool_.destroy(handle);
  }
}

void VulkanDevice::Queue::submit(VkFence fence) {
  VkSubmitInfo2KHR sub_info{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR,
      .flags = 0,
      .waitSemaphoreInfoCount = (uint32_t)wait_semaphores.size(),
      .pWaitSemaphoreInfos = wait_semaphores.data(),
      .commandBufferInfoCount = (uint32_t)submit_cmd_bufs.size(),
      .pCommandBufferInfos = submit_cmd_bufs.data(),
      .signalSemaphoreInfoCount = (uint32_t)signal_semaphores.size(),
      .pSignalSemaphoreInfos = signal_semaphores.data(),
  };

  VK_CHECK(vkQueueSubmit2KHR(queue, 1, &sub_info, fence));

  wait_semaphores.clear();
  submit_cmd_bufs.clear();
  signal_semaphores.clear();

  if (!present_swapchains.empty()) {
    VkPresentInfoKHR present_info{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = (uint32_t)present_wait_semaphores.size(),
        .pWaitSemaphores = present_wait_semaphores.data(),
        .swapchainCount = (uint32_t)present_swapchains.size(),
        .pSwapchains = present_swapchains.data(),
        .pImageIndices = present_swapchain_img_indices.data(),
    };
    VK_CHECK(vkQueuePresentKHR(queue, &present_info));

    present_wait_semaphores.clear();
    present_swapchains.clear();
    present_swapchain_img_indices.clear();
  }
}

bool VulkanDevice::recreate_swapchain(const rhi::SwapchainDesc& desc, rhi::Swapchain* swap) {
  auto* swapchain = (VulkanSwapchain*)swap;
  VkSurfaceCapabilitiesKHR surface_properties;
  VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, swapchain->surface_,
                                                     &surface_properties));
  uint32_t format_count = 0;
  VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, swapchain->surface_,
                                                &format_count, nullptr));
  // TODO: not thread safe
  static std::vector<VkSurfaceFormatKHR> formats;
  formats.resize(format_count);
  VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, swapchain->surface_,
                                                &format_count, formats.data()));
  VkFormat preferred_formats[] = {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB};
  VkFormat selected_format{};

  // TODO: color spaces.
  for (uint32_t i = 0; i < format_count; i++) {
    for (auto preferred_format : preferred_formats) {
      if (formats[i].format == preferred_format) {
        selected_format = formats[i].format;
        break;
      }
    }
  }

  uint32_t present_mode_count = 0;
  VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, swapchain->surface_,
                                                     &present_mode_count, nullptr));
  VkPresentModeKHR present_modes[16];
  ASSERT(present_mode_count <= 16);
  VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, swapchain->surface_,
                                                     &present_mode_count, present_modes));

  // TODO: look at KHR_present_mode_fifo_latest_ready
  VkPresentModeKHR selected_present_mode{VK_PRESENT_MODE_FIFO_KHR};
  // prefer mailbox for vsync on
  VkPresentModeKHR preferred_present_mode{desc.vsync ? VK_PRESENT_MODE_MAILBOX_KHR
                                                     : VK_PRESENT_MODE_IMMEDIATE_KHR};
  for (uint32_t i = 0; i < present_mode_count; i++) {
    if (present_modes[i] == preferred_present_mode) {
      selected_present_mode = preferred_present_mode;
      break;
    }
  }

  uint32_t create_w{}, create_h{};
  if (surface_properties.currentExtent.width == UINT32_MAX ||
      surface_properties.currentExtent.height == UINT32_MAX) {
    create_w = desc.width;
    create_h = desc.height;
  } else {
    create_w = surface_properties.currentExtent.width;
    create_h = surface_properties.currentExtent.height;
  }

  VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  if (surface_properties.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) {
    composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  } else if (surface_properties.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
    composite = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
  } else if (surface_properties.supportedCompositeAlpha &
             VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) {
    composite = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
  } else if (surface_properties.supportedCompositeAlpha &
             VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) {
    composite = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
  }
  rhi::TextureUsageFlags usage = rhi::TextureUsageColorAttachment | rhi::TextureUsageTransferSrc |
                                 rhi::TextureUsageTransferDst | rhi::TextureUsageStorage;
  VkSwapchainKHR old_swapchain = swapchain->swapchain_;
  VkSwapchainCreateInfoKHR swap_info{
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = swapchain->surface_,
      .minImageCount = std::min<uint32_t>(surface_properties.maxImageCount,
                                          surface_properties.minImageCount + 1),
      .imageFormat = selected_format,
      .imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
      .imageExtent = {create_w, create_h},
      .imageArrayLayers = 1,
      .imageUsage = convert(usage),
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 1,
      .pQueueFamilyIndices = &queues_[(int)rhi::QueueType::Graphics].family_idx,
      .preTransform = surface_properties.currentTransform,
      .compositeAlpha = composite,
      .presentMode = selected_present_mode,
      .clipped = VK_TRUE,
      .oldSwapchain = old_swapchain,
  };
  VK_CHECK(vkCreateSwapchainKHR(device_, &swap_info, nullptr, &swapchain->swapchain_));

  if (old_swapchain) {
    vkDestroySwapchainKHR(device_, old_swapchain, nullptr);
  }

  {  // swapchain images
    uint32_t swapchain_image_count{};
    VK_CHECK(
        vkGetSwapchainImagesKHR(device_, swapchain->swapchain_, &swapchain_image_count, nullptr));
    VkImage swapchain_images[16];
    ASSERT(swapchain_image_count <= 16);
    VK_CHECK(vkGetSwapchainImagesKHR(device_, swapchain->swapchain_, &swapchain_image_count,
                                     swapchain_images));
    rhi::TextureDesc tex_desc{
        .format = convert_format(selected_format),
        .storage_mode = rhi::StorageMode::GPUOnly,
        .usage = usage,
        .dims = {swap_info.imageExtent.width, swap_info.imageExtent.height, 1},
        .mip_levels = 1,
        .array_length = 1,
        .bindless = false,
    };
    for (uint32_t i = 0; i < swapchain_image_count; i++) {
      if (swapchain->textures_[i].is_valid()) {
        destroy(swapchain->textures_[i]);
      }
      auto tex_handle =
          texture_pool_.alloc(tex_desc, rhi::k_invalid_bindless_idx, swapchain_images[i],
                              VmaAllocation{}, VmaAllocationInfo{}, true);
      auto* tex = (VulkanTexture*)get_tex(tex_handle);
      tex->default_view_ = create_img_view(*tex, VK_IMAGE_VIEW_TYPE_2D,
                                           {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                            .baseMipLevel = 0,
                                            .levelCount = 1,
                                            .baseArrayLayer = 0,
                                            .layerCount = 1});
      ASSERT(tex->default_view_);
      swapchain->textures_[i] = tex_handle;
    }
    swapchain->swapchain_tex_count_ = swapchain_image_count;

    for (VkSemaphore sem : swapchain->acquire_semaphores_) {
      if (sem) del_q_.enqueue(sem);
    }
    for (uint32_t i = 0; i < swapchain_image_count; i++) {
      swapchain->acquire_semaphores_[i] =
          create_semaphore(("swapchain_acquire_semaphore_" + std::to_string(i)).c_str());
    }
    for (VkSemaphore sem : swapchain->ready_to_present_semaphores_) {
      if (sem) del_q_.enqueue(sem);
    }
    for (uint32_t i = 0; i < swapchain_image_count; i++) {
      swapchain->ready_to_present_semaphores_[i] =
          create_semaphore(("swapchain_present_semaphore_" + std::to_string(i)).c_str());
    }
  }

  swapchain->desc_ = desc;

  return true;
}

void VulkanDevice::set_vk_debug_name(VkObjectType object_type, uint64_t object_handle,
                                     const char* name) {
  VkDebugUtilsObjectNameInfoEXT name_info{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
      .objectType = object_type,
      .objectHandle = object_handle,
      .pObjectName = name,
  };
  VK_CHECK(vkSetDebugUtilsObjectNameEXT(device_, &name_info));
}

VkSemaphore VulkanDevice::create_semaphore(const char* name) {
  VkSemaphoreCreateInfo sem_cinfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkSemaphore result;
  VK_CHECK(vkCreateSemaphore(device_, &sem_cinfo, nullptr, &result));
  if (name) {
    set_vk_debug_name(VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)result, name);
  }
  return result;
}

rhi::PipelineHandle VulkanDevice::create_compute_pipeline(const rhi::ShaderCreateInfo& cinfo) {
  auto spirv_code = read_file_to_uint_vec((shader_lib_dir_ / cinfo.path).concat(".comp.spv"));

  VkDescriptorSetLayout layout;
  // TODO: don't allocate?
  std::vector<VkDescriptorSetLayoutBinding> layout_bindings;
  {
    spv_reflect::ShaderModule refl{spirv_code, SPV_REFLECT_MODULE_FLAG_NO_COPY};
    auto result = refl.GetResult();
    ASSERT(result == SPV_REFLECT_RESULT_SUCCESS);
    uint32_t set_count;
    refl.EnumerateDescriptorSets(&set_count, nullptr);
    std::vector<SpvReflectDescriptorSet*> sets;

    VkDescriptorSetLayout layouts[16];
    VkPushConstantRange push_constant_ranges[4];
    VkPipelineLayoutCreateInfo pipeline_layout_cinfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .setLayoutCount = 0,
        .pSetLayouts = layouts,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = push_constant_ranges,
    };

    uint32_t pc_count;
    refl.EnumeratePushConstantBlocks(&pc_count, nullptr);
    std::vector<SpvReflectBlockVariable*> push_constants(pc_count);
    refl.EnumeratePushConstantBlocks(&pc_count, push_constants.data());

    for (uint32_t i = 0; i < pc_count; i++) {
      auto& pc = push_constants[i];
      VkPushConstantRange range{
          // TODO: lolllll
          .stageFlags = VK_SHADER_STAGE_ALL,
          .offset = pc->offset,
          .size = pc->size,
      };
      LINFO("PC Range: offset {}, size {}", range.offset, range.size);
      push_constant_ranges[pipeline_layout_cinfo.pushConstantRangeCount++] = range;
    }

    sets.resize(set_count);
    refl.EnumerateDescriptorSets(&set_count, sets.data());
    ASSERT(set_count == 1);
    for (uint32_t i = 0; i < set_count; i++) {
      auto& set = sets[i];
      LINFO("set {}: binding count {}", set->set, set->binding_count);
      for (uint32_t j = 0; j < set->binding_count; j++) {
        auto& binding = set->bindings[j];
        LINFO("  binding {}: type {}, count {}", binding->binding,
              string_VkDescriptorType((VkDescriptorType)binding->descriptor_type), binding->count);
        layout_bindings.emplace_back(VkDescriptorSetLayoutBinding{
            .binding = binding->binding,
            .descriptorType = (VkDescriptorType)binding->descriptor_type,
            .descriptorCount = binding->count,
            .stageFlags = (VkShaderStageFlagBits)refl.GetShaderStage(),
            .pImmutableSamplers = nullptr,
        });
      }
      VkDescriptorSetLayoutCreateInfo layout_cinfo{
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
          .bindingCount = set->binding_count,
          .pBindings = layout_bindings.data(),
      };

      auto hash = hash_descriptor_set_layout_cinfo(layout_cinfo);
      auto it = set_layout_cache_.find(hash);

      if (it != set_layout_cache_.end()) {
        layout = it->second;
      } else {
        VK_CHECK(vkCreateDescriptorSetLayout(device_, &layout_cinfo, nullptr, &layout));
        set_layout_cache_[hash] = layout;
      }
      layouts[pipeline_layout_cinfo.setLayoutCount++] = layout;
    }

    VK_CHECK(vkCreatePipelineLayout(device_, &pipeline_layout_cinfo, nullptr,
                                    &default_pipeline_layout_));
  }

  VkShaderModule module = create_shader_module(spirv_code);

  VkPipelineShaderStageCreateInfo stage_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = module,
      .pName = cinfo.entry_point.c_str(),
  };

  VkComputePipelineCreateInfo pipeline_cinfo{
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = stage_info,
      .layout = default_pipeline_layout_,
  };

  VkPipeline vk_pipeline;
  VK_CHECK(vkCreateComputePipelines(device_, nullptr, 1, &pipeline_cinfo, nullptr, &vk_pipeline));

  vkDestroyShaderModule(device_, module, nullptr);

  return pipeline_pool_.alloc(cinfo, vk_pipeline, default_pipeline_layout_, layout,
                              std::move(layout_bindings));
}

VkShaderModule VulkanDevice::create_shader_module(const std::filesystem::path& path) {
  auto spirv_code = read_file_to_uint_vec(path);

  return create_shader_module(spirv_code);
}

uint64_t VulkanDevice::hash_descriptor_set_layout_cinfo(
    const VkDescriptorSetLayoutCreateInfo& cinfo) {
  uint64_t hash = 1;
  for (const auto& binding : std::span(cinfo.pBindings, cinfo.bindingCount)) {
    util::hash::hash_combine(hash, binding.binding);
    util::hash::hash_combine(hash, binding.descriptorType);
    util::hash::hash_combine(hash, binding.descriptorCount);
    util::hash::hash_combine(hash, binding.stageFlags);
  }
  return hash;
}

VkShaderModule VulkanDevice::create_shader_module(std::span<const uint32_t> spirv_code) {
  VkShaderModuleCreateInfo module_cinfo{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = spirv_code.size() * sizeof(uint32_t),
      .pCode = spirv_code.data(),
  };
  VkShaderModule module;
  VK_CHECK(vkCreateShaderModule(device_, &module_cinfo, nullptr, &module));
  return module;
}

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
