#include "VulkanDevice.hpp"

// clang-format off
#include <volk.h>
#include <VkBootstrap.h>
#include <fstream>
// clang-format on

#include "VMAWrapper.hpp"  // IWYU pragma: keep
#include "Window.hpp"
#include "core/EAssert.hpp"
#include "core/Logger.hpp"
#include "core/Util.hpp"
#include "gfx/GFXTypes.hpp"
#include "gfx/vulkan/VulkanCommon.hpp"
#include "vulkan/vulkan_core.h"

#include "core/Config.hpp"

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
    case rhi::ShaderType::Object:
      return VK_SHADER_STAGE_TASK_BIT_EXT;
    default:
      return VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
  }
}
std::vector<uint32_t> read_file_to_uint_vec(const std::filesystem::path& path) {
  std::vector<uint32_t> result;

  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    LERROR("Error reading file: {}", path.c_str());
    return result;
  }

  file.seekg(std::ios::end);
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
    case TextureFormat::D32float:
      return VK_FORMAT_D32_SFLOAT;
    case TextureFormat::R32float:
      return VK_FORMAT_R32_SFLOAT;
    case TextureFormat::Undefined:
    default:
      return VK_FORMAT_UNDEFINED;
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

  exit(1);
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
  for (size_t i = 0; i < info_.frames_in_flight; i++) {
    vkDestroyCommandPool(device_, command_pools_[i], nullptr);
  }
  vmaDestroyAllocator(allocator_);
  vkDestroyDevice(device_, nullptr);
  vkDestroySurfaceKHR(instance_, surface_, nullptr);
  vkb::destroy_instance(vkb_inst_);
}

void VulkanDevice::init(const InitInfo& init_info) {
  info_.frames_in_flight = init_info.frames_in_flight;
  vkb::InstanceBuilder builder{};

  VK_CHECK(volkInitialize());

  if (init_info.app_name.size()) {
    builder.set_app_name(init_info.app_name.c_str());
  }

  constexpr int min_api_version_major = 1;
  constexpr int min_api_version_minor = 2;

  auto inst_ret = builder.request_validation_layers(init_info.validation_layers_enabled)
                      .set_minimum_instance_version(min_api_version_major, min_api_version_minor, 0)
                      .require_api_version(min_api_version_major, min_api_version_minor, 0)
                      .set_debug_callback(vk_debug_callback)
                      .build();
  check_vkb_result("Failed to get vulkan instance", inst_ret);
  vkb_inst_ = inst_ret.value();
  instance_ = vkb_inst_.instance;

  volkLoadInstance(instance_);

  glfwCreateWindowSurface(instance_, init_info.window->get_handle(), nullptr, &surface_);
  if (!surface_) {
    LCRITICAL("Failed to create surface");
    exit(1);
  }

  vkb::PhysicalDeviceSelector phys_device_selector{vkb_inst_};
  auto phys_ret = phys_device_selector.set_surface(surface_)
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
      vkGetDeviceQueue(device_, i, 0, &queues_[QueueType_Graphics].queue);
      found_graphics_queue = true;
      break;
    }
  }

  if (!found_graphics_queue) {
    LCRITICAL("Failed to find graphics queue");
    exit(1);
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
        .queueFamilyIndex = queues_[QueueType_Graphics].family_idx,
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
  //
  // {  // default pipeline layout
  //   VkPushConstantRange pc_range{.stageFlags = VK_SHADER_STAGE_ALL, .offset = 0, .size = 120};
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
  VK_CHECK(vmaCreateImage(allocator_, &cinfo, &alloc_info, &image, &allocation, nullptr));
  return texture_pool_.alloc(desc, rhi::k_invalid_bindless_idx, image, allocation, allocation_info);
}

rhi::CmdEncoder* VulkanDevice::begin_command_list() {
  if (curr_cmd_encoder_i_ >= cmd_encoders_.size()) {
    VkCommandBuffer vk_cmd_buf;
    VkCommandBufferAllocateInfo info{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    VK_CHECK(vkAllocateCommandBuffers(device_, &info, &vk_cmd_buf));
    // TODO: pipeline layout
    cmd_encoders_.emplace_back(std::make_unique<VulkanCmdEncoder>(vk_cmd_buf, nullptr));
  }
  auto& enc = cmd_encoders_[curr_cmd_encoder_i_];
  VkCommandBufferBeginInfo begin_info{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
  VK_CHECK(vkBeginCommandBuffer(enc->cmd_buf_, &begin_info));

  return enc.get();
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

    auto spirv_code = read_file_to_uint_vec(shader_info.path);
    VkShaderModuleCreateInfo module_cinfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirv_code.size() * sizeof(uint32_t),
        .pCode = spirv_code.data(),
    };
    VkShaderModule module;
    VK_CHECK(vkCreateShaderModule(device_, &module_cinfo, nullptr, &module));
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
  exit(1);
  return pipeline_pool_.alloc();
}

}  // namespace gfx::vk

} // namespace TENG_NAMESPACE
