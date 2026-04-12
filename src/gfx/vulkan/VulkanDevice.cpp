#include "VulkanDevice.hpp"

// clang-format off
#include <volk.h>
#include <VkBootstrap.h>
#include <algorithm>
#include <format>
#include <fstream>
#include <mutex>
#include <tracy/Tracy.hpp>
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
#include "gfx/rhi/Texture.hpp"
#include "gfx/vulkan/VkUtil.hpp"
#include "gfx/vulkan/VulkanCommon.hpp"
#include "spirv_reflect.h"
#include "vulkan/vulkan_core.h"

namespace TENG_NAMESPACE {

namespace gfx::vk {

namespace {

VkBorderColor convert_border_color(rhi::BorderColor border_color) {
  switch (border_color) {
    case rhi::BorderColor::FloatTransparentBlack:
      return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    case rhi::BorderColor::FloatOpaqueBlack:
      return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    case rhi::BorderColor::FloatOpaqueWhite:
      return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    case rhi::BorderColor::IntTransparentBlack:
      return VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
    case rhi::BorderColor::IntOpaqueBlack:
      return VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    case rhi::BorderColor::IntOpaqueWhite:
      return VK_BORDER_COLOR_INT_OPAQUE_WHITE;
    default:
      ASSERT(0);
      return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
  }
}
VkSamplerMipmapMode convert_mipmap_mode(rhi::FilterMode filter) {
  switch (filter) {
    case rhi::FilterMode::Linear:
      return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    case rhi::FilterMode::Nearest:
      return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    default:
      ASSERT(0);
  }
}

VkFilter convert_filter_mode(rhi::FilterMode filter) {
  switch (filter) {
    case rhi::FilterMode::Linear:
      return VK_FILTER_LINEAR;
    case rhi::FilterMode::Nearest:
      return VK_FILTER_NEAREST;
    default:
      ASSERT(0);
  }
}

VkSamplerAddressMode convert_address_mode(rhi::AddressMode mode) {
  switch (mode) {
    case rhi::AddressMode::Repeat:
      return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case rhi::AddressMode::MirroredRepeat:
      return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case rhi::AddressMode::ClampToEdge:
      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case rhi::AddressMode::ClampToBorder:
      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case rhi::AddressMode::MirrorClampToEdge:
      return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    default:
      ASSERT(0);
  }
}

std::filesystem::path get_spv_path(const std::filesystem::path& shader_lib_dir,
                                   const std::filesystem::path& path, const rhi::ShaderType type) {
  const char* type_str{};
  switch (type) {
    case rhi::ShaderType::Fragment:
      type_str = "frag";
      break;
    case rhi::ShaderType::Vertex:
      type_str = "vert";
      break;
    case rhi::ShaderType::Mesh:
      type_str = "mesh";
      break;
    case rhi::ShaderType::Task:
      type_str = "task";
      break;
    case rhi::ShaderType::Compute:
      type_str = "comp";
      break;
    default:
      ASSERT(0);
      break;
  }
  return (shader_lib_dir / path).concat(".").concat(type_str).concat(".spv");
}

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
    case TextureFormat::R16G16B16A16Sfloat:
      return VK_FORMAT_R16G16B16A16_SFLOAT;
    case TextureFormat::R32G32B32A32Sfloat:
      return VK_FORMAT_R32G32B32A32_SFLOAT;
    case TextureFormat::Undefined:
      return VK_FORMAT_UNDEFINED;
    case TextureFormat::ASTC4x4UnormBlock:
      return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
    case TextureFormat::ASTC4x4SrgbBlock:
      return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
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
    case VK_FORMAT_R16G16B16A16_SFLOAT:
      return TextureFormat::R16G16B16A16Sfloat;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
      return TextureFormat::R32G32B32A32Sfloat;
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
  if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    ALWAYS_ASSERT(0);
  }

  return VK_FALSE;
}

void check_vkb_result(const char* err_msg, const auto& vkb_result) {
  if (!vkb_result) {
    LCRITICAL("{}: {}", err_msg, vkb_result.error().message());
    exit(1);
  }
}

}  // namespace

rhi::GpuAdapterInfo VulkanDevice::query_gpu_adapter_info() const {
  rhi::GpuAdapterInfo out{};
  if (physical_device_ == VK_NULL_HANDLE) {
    return out;
  }
  VkPhysicalDeviceProperties props{};
  vkGetPhysicalDeviceProperties(physical_device_, &props);
  out.name = props.deviceName;
  switch (props.deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
      out.kind = rhi::GpuAdapterKind::Integrated;
      break;
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
      out.kind = rhi::GpuAdapterKind::Discrete;
      break;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
      out.kind = rhi::GpuAdapterKind::Virtual;
      break;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
      out.kind = rhi::GpuAdapterKind::Cpu;
      break;
    case VK_PHYSICAL_DEVICE_TYPE_OTHER:
    default:
      out.kind = rhi::GpuAdapterKind::Other;
      break;
  }
  out.api_version =
      std::format("{}.{}.{}", VK_VERSION_MAJOR(props.apiVersion),
                  VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion));
  out.driver_version =
      std::format("{}.{}.{}", VK_VERSION_MAJOR(props.driverVersion),
                  VK_VERSION_MINOR(props.driverVersion), VK_VERSION_PATCH(props.driverVersion));
  out.vendor_id = props.vendorID;
  out.device_id = props.deviceID;
  return out;
}

void VulkanDevice::shutdown() {
  vkDeviceWaitIdle(device_);

  del_q_.flush(SIZE_MAX);

  for (VkSampler sampler : immutable_samplers_) {
    vkDestroySampler(device_, sampler, nullptr);
  }

  for (auto& cmd : cmd_encoders_) {
    for (uint32_t pool_i = 0; pool_i < frames_in_flight(); pool_i++) {
      cmd->binder_pools_[pool_i].destroy(*this);
    }
  }
  for (auto& [a, pipeline] : all_pipelines_) {
    vkDestroyPipeline(device_, ((VulkanPipeline*)get_pipeline(pipeline))->pipeline_, nullptr);
  }

  for (auto& [hash, entry] : pipeline_layout_cache_) {
    vkDestroyPipelineLayout(device_, entry.layout, nullptr);
  }
  pipeline_layout_cache_.clear();

  for (auto& [hash, layout] : set_layout_cache_) {
    vkDestroyDescriptorSetLayout(device_, layout, nullptr);
  }
  set_layout_cache_.clear();

  shutdown_bindless_heaps();

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

  VkPhysicalDeviceFeatures feat{};
  feat.samplerAnisotropy = true;
  feat.shaderInt64 = true;

  phys_device_selector.set_required_features(feat);

  VkPhysicalDeviceVulkan12Features feat12{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  feat12.descriptorIndexing = VK_TRUE;
  feat12.shaderUniformTexelBufferArrayDynamicIndexing = VK_TRUE;
  feat12.shaderStorageTexelBufferArrayDynamicIndexing = VK_TRUE;
  feat12.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
  feat12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
  feat12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
  feat12.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
  feat12.shaderInputAttachmentArrayNonUniformIndexing = VK_TRUE;
  feat12.shaderUniformTexelBufferArrayNonUniformIndexing = VK_TRUE;
  feat12.shaderStorageTexelBufferArrayNonUniformIndexing = VK_TRUE;
  feat12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
  feat12.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
  feat12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
  feat12.descriptorBindingUniformTexelBufferUpdateAfterBind = VK_TRUE;
  feat12.descriptorBindingStorageTexelBufferUpdateAfterBind = VK_TRUE;
  feat12.descriptorBindingPartiallyBound = VK_TRUE;
  feat12.descriptorBindingVariableDescriptorCount = VK_TRUE;
  feat12.runtimeDescriptorArray = VK_TRUE;
  feat12.scalarBlockLayout = VK_TRUE;
  phys_device_selector.set_required_features_12(feat12);

  VkPhysicalDeviceVulkan13Features feat13{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .shaderDemoteToHelperInvocation = true,
      .synchronization2 = true,
      .dynamicRendering = true,
  };
  phys_device_selector.set_required_features_13(feat13);

  VkPhysicalDeviceExtendedDynamicStateFeaturesEXT ext_state{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
      .extendedDynamicState = true,
  };
  phys_device_selector.add_required_extension_features(ext_state);

  VkPhysicalDeviceMeshShaderFeaturesEXT mesh_shader_features{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT,
      .taskShader = VK_TRUE,
      .meshShader = VK_TRUE,
  };
  phys_device_selector.add_required_extension_features(mesh_shader_features);

  const char* required_extensions[] = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME,           VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
      VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,   VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
      VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME, VK_EXT_MESH_SHADER_EXTENSION_NAME,
  };
  phys_device_selector.add_required_extensions(ARRAY_SIZE(required_extensions),
                                               required_extensions);

  auto phys_ret = phys_device_selector.defer_surface_initialization()
                      .set_minimum_version(min_api_version_major, min_api_version_minor)
                      .select();
  check_vkb_result("Failed to select physical device", phys_ret);
  physical_device_ = phys_ret.value().physical_device;

  {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical_device_, &props);

    LINFO("Selected GPU: {}", props.deviceName);
    LINFO("  API Version: {}.{}.{}", VK_VERSION_MAJOR(props.apiVersion),
          VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion));
    LINFO("  Driver Version: {}.{}.{}", VK_VERSION_MAJOR(props.driverVersion),
          VK_VERSION_MINOR(props.driverVersion), VK_VERSION_PATCH(props.driverVersion));
    LINFO("  Vendor ID: {:#x}", props.vendorID);
    LINFO("  Device ID: {:#x}", props.deviceID);

    const char* deviceTypeStr = nullptr;
    switch (props.deviceType) {
      case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        deviceTypeStr = "Integrated";
        break;
      case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        deviceTypeStr = "Discrete";
        break;
      case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        deviceTypeStr = "Virtual";
        break;
      case VK_PHYSICAL_DEVICE_TYPE_CPU:
        deviceTypeStr = "CPU";
        break;
      default:
        deviceTypeStr = "Other";
        break;
    }
    LINFO("  Device Type: {}", deviceTypeStr);
  }

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
      queue_family_indices_.push_back(i);
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

  VkPhysicalDeviceMemoryProperties2 mem_props2{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
  };
  // https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#memory-device
  //  In a unified memory architecture (UMA) system there is often only a single memory heap which
  //  is considered to be equally “local” to the host and to the device, and such an
  //  implementation must advertise the heap as device-local.
  vkGetPhysicalDeviceMemoryProperties2(physical_device_, &mem_props2);
  if (mem_props2.memoryProperties.memoryHeapCount == 1 &&
      mem_props2.memoryProperties.memoryHeaps[0].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
    auto count = mem_props2.memoryProperties.memoryTypeCount;
    for (auto i = 0u; i < count; i++) {
      auto flags = mem_props2.memoryProperties.memoryTypes[i].propertyFlags;
      if (flags & (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
        capabilities_ |= rhi::GraphicsCapability::CacheCoherentUMA;
      }
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

  del_q_.init(device_, allocator_, info_.frames_in_flight);
  {
    auto add_immutable_sampler = [&](const rhi::SamplerDesc& desc) {
      auto actual_desc = desc;
      actual_desc.flags |= rhi::SamplerDescFlags::NoBindless;
      immutable_samplers_.emplace_back(create_vk_sampler(actual_desc));
    };
    add_immutable_sampler({
        .min_filter = rhi::FilterMode::Linear,
        .mag_filter = rhi::FilterMode::Linear,
        .mipmap_mode = rhi::FilterMode::Linear,
        .address_mode = rhi::AddressMode::ClampToEdge,
    });
    add_immutable_sampler({
        .min_filter = rhi::FilterMode::Linear,
        .mag_filter = rhi::FilterMode::Linear,
        .mipmap_mode = rhi::FilterMode::Linear,
        .address_mode = rhi::AddressMode::Repeat,
    });
    add_immutable_sampler({
        .min_filter = rhi::FilterMode::Linear,
        .mag_filter = rhi::FilterMode::Linear,
        .mipmap_mode = rhi::FilterMode::Linear,
        .address_mode = rhi::AddressMode::MirroredRepeat,
    });
    add_immutable_sampler({
        .min_filter = rhi::FilterMode::Nearest,
        .mag_filter = rhi::FilterMode::Nearest,
        .mipmap_mode = rhi::FilterMode::Nearest,
        .address_mode = rhi::AddressMode::ClampToEdge,
    });
    add_immutable_sampler({
        .min_filter = rhi::FilterMode::Nearest,
        .mag_filter = rhi::FilterMode::Nearest,
        .mipmap_mode = rhi::FilterMode::Nearest,
        .address_mode = rhi::AddressMode::Repeat,
    });
    add_immutable_sampler({
        .min_filter = rhi::FilterMode::Nearest,
        .mag_filter = rhi::FilterMode::Nearest,
        .mipmap_mode = rhi::FilterMode::Nearest,
        .address_mode = rhi::AddressMode::MirroredRepeat,
    });
    add_immutable_sampler({
        .min_filter = rhi::FilterMode::Linear,
        .mag_filter = rhi::FilterMode::Linear,
        .mipmap_mode = rhi::FilterMode::Linear,
        .address_mode = rhi::AddressMode::ClampToEdge,
        .anisotropy_enable = true,
        .max_anisotropy = 16.0f,
    });
    add_immutable_sampler({
        .min_filter = rhi::FilterMode::Linear,
        .mag_filter = rhi::FilterMode::Linear,
        .mipmap_mode = rhi::FilterMode::Linear,
        .address_mode = rhi::AddressMode::Repeat,
        .anisotropy_enable = true,
        .max_anisotropy = 16.0f,
    });
    add_immutable_sampler({
        .min_filter = rhi::FilterMode::Linear,
        .mag_filter = rhi::FilterMode::Linear,
        .mipmap_mode = rhi::FilterMode::Linear,
        .address_mode = rhi::AddressMode::MirroredRepeat,
        .anisotropy_enable = true,
        .max_anisotropy = 16.0f,
    });
    add_immutable_sampler({
        .min_filter = rhi::FilterMode::Linear,
        .mag_filter = rhi::FilterMode::Linear,
        .mipmap_mode = rhi::FilterMode::Nearest,
        .address_mode = rhi::AddressMode::ClampToEdge,
        .compare_enable = true,
        .compare_op = rhi::CompareOp::GreaterOrEqual,
    });
  }

  init_bindless_heaps();
}

rhi::SamplerHandle VulkanDevice::create_sampler(const rhi::SamplerDesc& desc) {
  VkSampler sampler = create_vk_sampler(desc);
  uint32_t bindless_idx = rhi::k_invalid_bindless_idx;
  if (!rhi::has_flag(desc.flags, rhi::SamplerDescFlags::NoBindless)) {
    int idx = alloc_bindless_sampler_idx();
    ALWAYS_ASSERT(idx >= 0);
    bindless_idx = static_cast<uint32_t>(idx);
  }
  auto handle = sampler_pool_.alloc(desc, sampler, bindless_idx);
  LINFO("creating sampler {}", bindless_idx);
  if (bindless_idx != rhi::k_invalid_bindless_idx) {
    write_bindless_sampler(bindless_idx, sampler);
  }
  return handle;
}

rhi::BufferHandle VulkanDevice::create_buf(const rhi::BufferDesc& desc) {
  VkBufferCreateInfo cinfo{
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = desc.size,
      .sharingMode = queue_family_indices_.size() == 1 ? VK_SHARING_MODE_EXCLUSIVE
                                                       : VK_SHARING_MODE_CONCURRENT,
      .queueFamilyIndexCount = (uint32_t)queue_family_indices_.size(),
      .pQueueFamilyIndices = queue_family_indices_.data(),
  };
  if (has_flag(desc.usage, rhi::BufferUsage::Index)) {
    cinfo.usage |= VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT;
  }
  if (has_flag(desc.usage, rhi::BufferUsage::Indirect)) {
    cinfo.usage |= VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT;
  }
  if (has_flag(desc.usage, rhi::BufferUsage::Storage)) {
    cinfo.usage |= VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT;
  }
  if (has_flag(desc.usage, rhi::BufferUsage::Uniform)) {
    cinfo.usage |= VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT;
  }
  if (has_flag(desc.usage, rhi::BufferUsage::Vertex)) {
    cinfo.usage |= VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT;
  }

  VmaAllocationCreateInfo vma_cinfo{};
  if (rhi::has_flag(desc.flags, rhi::BufferDescFlags::CPUAccessible) ||
      (has_flag(capabilities_, rhi::GraphicsCapability::CacheCoherentUMA) &&
       !has_flag(desc.flags, rhi::BufferDescFlags::DisableCPUAccessOnUMA))) {
    vma_cinfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    if (has_flag(desc.flags, rhi::BufferDescFlags::CPURandomAccess)) {
      vma_cinfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    } else {
      vma_cinfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }
  } else {
    ASSERT(cinfo.usage != 0);
  }

  cinfo.usage |= VK_BUFFER_USAGE_2_TRANSFER_DST_BIT | VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT;

  vma_cinfo.usage = VMA_MEMORY_USAGE_AUTO;
  VkBuffer buffer{};
  VmaAllocation allocation{};
  VK_CHECK(vmaCreateBuffer(allocator_, &cinfo, &vma_cinfo, &buffer, &allocation, nullptr));
  VmaAllocationInfo allocation_info{};
  vmaGetAllocationInfo(allocator_, allocation, &allocation_info);

  uint32_t bindless_idx = rhi::k_invalid_bindless_idx;
  if (has_flag(desc.usage, rhi::BufferUsage::Storage) &&
      !rhi::has_flag(desc.flags, rhi::BufferDescFlags::NoBindless)) {
    int idx = alloc_bindless_storage_idx();
    ALWAYS_ASSERT(idx >= 0);
    bindless_idx = static_cast<uint32_t>(idx);
  }

  auto handle = buffer_pool_.alloc(desc, bindless_idx, buffer, allocation, vma_cinfo.flags,
                                   allocation_info.pMappedData);
  if (bindless_idx != rhi::k_invalid_bindless_idx) {
    write_bindless_storage_descriptor(bindless_idx, buffer);
  }
  return handle;
}

rhi::TextureHandle VulkanDevice::create_tex(const rhi::TextureDesc& desc) {
  VkImageCreateInfo cinfo{.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ASSERT(desc.format != rhi::TextureFormat::Undefined);

  if (desc.dims.z > 1) {
    ASSERT(desc.array_length == 1);
    cinfo.imageType = VK_IMAGE_TYPE_3D;
  } else if (desc.dims.y > 1) {
    cinfo.imageType = VK_IMAGE_TYPE_2D;
  } else {
    cinfo.imageType = VK_IMAGE_TYPE_1D;
  }

  cinfo.format = convert_format(desc.format);
  cinfo.extent = VkExtent3D{desc.dims.x, desc.dims.y, desc.dims.z};
  cinfo.mipLevels = desc.mip_levels;
  cinfo.arrayLayers = desc.array_length;
  cinfo.samples = VK_SAMPLE_COUNT_1_BIT;
  cinfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  cinfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  cinfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  cinfo.usage = convert(desc.usage);
  cinfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

  VmaAllocationCreateInfo alloc_info{};
  alloc_info.usage = VMA_MEMORY_USAGE_AUTO;

  VkImage image{};
  VmaAllocation allocation;

  VkImageViewType view_type;
  if (desc.dims.z > 1) {
    view_type = VK_IMAGE_VIEW_TYPE_3D;
  } else if (desc.dims.y > 1) {
    view_type = desc.array_length > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
  } else {
    view_type = desc.array_length > 1 ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
  }

  VK_CHECK(vmaCreateImage(allocator_, &cinfo, &alloc_info, &image, &allocation, nullptr));
  ASSERT(image);

  if (desc.name) {
    set_vk_debug_name(VK_OBJECT_TYPE_IMAGE, (uint64_t)image, desc.name);
  }

  const bool want_bindless = !rhi::has_flag(desc.flags, rhi::TextureDescFlags::NoBindless);
  const bool use_sampled = want_bindless && rhi::has_flag(desc.usage, rhi::TextureUsage::Sample);
  const bool use_storage =
      want_bindless && (rhi::has_flag(desc.usage, rhi::TextureUsage::Storage) ||
                        rhi::has_flag(desc.usage, rhi::TextureUsage::ShaderWrite));

  uint32_t bindless_idx = rhi::k_invalid_bindless_idx;
  if (use_sampled || use_storage) {
    int idx = alloc_bindless_image_slot();
    ALWAYS_ASSERT(idx >= 0);
    bindless_idx = static_cast<uint32_t>(idx);
  }

  VkImageAspectFlags img_aspect;
  if (rhi::is_depth_format(desc.format)) {
    img_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
  } else {
    img_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
  }

  auto handle = texture_pool_.alloc(desc, bindless_idx, image, allocation, false);
  auto* tex = (VulkanTexture*)get_tex(handle);
  tex->default_view_ = create_img_view(*tex, view_type,
                                       {.aspectMask = img_aspect,
                                        .baseMipLevel = 0,
                                        .levelCount = desc.mip_levels,
                                        .baseArrayLayer = 0,
                                        .layerCount = desc.array_length});

  if (bindless_idx != rhi::k_invalid_bindless_idx) {
    if (use_sampled) {
      write_bindless_sampled_image(bindless_idx, tex->default_view_,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    } else {
      write_bindless_sampled_image(bindless_idx, null_image_view_,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    if (use_storage) {
      write_bindless_storage_image(bindless_idx, tex->default_view_, VK_IMAGE_LAYOUT_GENERAL);
    } else {
      write_bindless_storage_image(bindless_idx, null_image_view_, VK_IMAGE_LAYOUT_GENERAL);
    }
  }
  return handle;
}

namespace {

VkImageViewType vk_image_view_type_for_range(const rhi::TextureDesc& desc, uint32_t layer_count) {
  if (desc.dims.z > 1) {
    return VK_IMAGE_VIEW_TYPE_3D;
  }
  if (desc.dims.y > 1) {
    return layer_count > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
  }
  return layer_count > 1 ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
}

VkImageAspectFlags vk_image_aspect_mask_for_tex(rhi::TextureFormat fmt) {
  VkImageAspectFlags mask{};
  if (rhi::is_depth_format(fmt)) {
    mask |= VK_IMAGE_ASPECT_DEPTH_BIT;
  }
  if (rhi::is_stencil_format(fmt)) {
    mask |= VK_IMAGE_ASPECT_STENCIL_BIT;
  }
  if (mask == 0) {
    mask = VK_IMAGE_ASPECT_COLOR_BIT;
  }
  return mask;
}

}  // namespace

rhi::TextureViewHandle VulkanDevice::create_tex_view(rhi::TextureHandle handle,
                                                     uint32_t base_mip_level, uint32_t level_count,
                                                     uint32_t base_array_layer,
                                                     uint32_t layer_count) {
  auto* tex = get_vk_tex(handle);
  ALWAYS_ASSERT(tex);
  ALWAYS_ASSERT(tex->image_);
  ALWAYS_ASSERT(!tex->is_swapchain_image_);
  const rhi::TextureDesc& desc = tex->desc();
  ALWAYS_ASSERT(desc.dims.z <= 1);

  ALWAYS_ASSERT(level_count > 0);
  ALWAYS_ASSERT(layer_count > 0);
  ALWAYS_ASSERT(base_mip_level + level_count <= desc.mip_levels);
  ALWAYS_ASSERT(base_array_layer + layer_count <= desc.array_length);

  const VkImageViewType view_type = vk_image_view_type_for_range(desc, layer_count);
  const VkImageSubresourceRange range{
      .aspectMask = vk_image_aspect_mask_for_tex(desc.format),
      .baseMipLevel = base_mip_level,
      .levelCount = level_count,
      .baseArrayLayer = base_array_layer,
      .layerCount = layer_count,
  };
  VkImageView view = create_img_view(*tex, view_type, range);

  const bool want_bindless = !rhi::has_flag(desc.flags, rhi::TextureDescFlags::NoBindless);
  const bool use_sampled = want_bindless && rhi::has_flag(desc.usage, rhi::TextureUsage::Sample);
  const bool use_storage =
      want_bindless && (rhi::has_flag(desc.usage, rhi::TextureUsage::Storage) ||
                        rhi::has_flag(desc.usage, rhi::TextureUsage::ShaderWrite));

  uint32_t bindless_idx = rhi::k_invalid_bindless_idx;
  if (use_sampled || use_storage) {
    int idx = alloc_bindless_image_slot();
    ALWAYS_ASSERT(idx >= 0);
    bindless_idx = static_cast<uint32_t>(idx);
  }
  if (bindless_idx != rhi::k_invalid_bindless_idx) {
    if (use_sampled) {
      write_bindless_sampled_image(bindless_idx, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    } else {
      write_bindless_sampled_image(bindless_idx, null_image_view_,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    if (use_storage) {
      write_bindless_storage_image(bindless_idx, view, VK_IMAGE_LAYOUT_GENERAL);
    } else {
      write_bindless_storage_image(bindless_idx, null_image_view_, VK_IMAGE_LAYOUT_GENERAL);
    }
  }

  const auto sub_id = static_cast<rhi::TextureViewHandle>(tex->tex_views.size());
  tex->tex_views.push_back({view, bindless_idx});
  return sub_id;
}

uint32_t VulkanDevice::get_tex_view_bindless_idx(rhi::TextureHandle handle, int subresource_id) {
  auto* tex = get_vk_tex(handle);
  ALWAYS_ASSERT(tex);
  ALWAYS_ASSERT(subresource_id >= 0);
  ALWAYS_ASSERT(subresource_id < static_cast<int>(tex->tex_views.size()));
  const auto& tv = tex->tex_views[static_cast<size_t>(subresource_id)];
  ALWAYS_ASSERT(tv.view != VK_NULL_HANDLE);
  ALWAYS_ASSERT(tv.bindless_idx != rhi::k_invalid_bindless_idx);
  return tv.bindless_idx;
}

VkImageView VulkanDevice::get_vk_tex_view(rhi::TextureHandle handle, int subresource_id) {
  auto* tex = get_vk_tex(handle);
  ALWAYS_ASSERT(tex);
  if (subresource_id < 0) {
    return tex->default_view_;
  }
  ALWAYS_ASSERT(subresource_id < static_cast<int>(tex->tex_views.size()));
  VkImageView v = tex->tex_views[static_cast<size_t>(subresource_id)].view;
  ALWAYS_ASSERT(v != VK_NULL_HANDLE);
  return v;
}

void VulkanDevice::destroy(rhi::TextureHandle tex_handle, int tex_view_handle) {
  auto* tex = get_vk_tex(tex_handle);
  if (!tex) {
    return;
  }
  ASSERT(tex_view_handle >= 0);
  ASSERT(tex_view_handle < static_cast<int>(tex->tex_views.size()));
  auto& tv = tex->tex_views[static_cast<size_t>(tex_view_handle)];
  if (tv.view == VK_NULL_HANDLE) {
    return;
  }
  uint32_t bi = tv.bindless_idx;
  if (bi != rhi::k_invalid_bindless_idx && bi != 0u) {
    clear_bindless_image_slot(bi);
    free_bindless_image_slot(bi);
  }
  del_q_.enqueue(tv.view);
  tv = {};
}

rhi::CmdEncoder* VulkanDevice::begin_cmd_encoder(rhi::QueueType queue_type) {
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
  if (curr_cmd_encoder_i_ == 0) {
    indexed_indirect_pc_cache_[frame_idx()].slots.clear();
  }
  static int i = 0;
  if (i++ == 0) {
    LWARN("Copy queue not supported yet, defaulting to graphics queue");
    queue_type = rhi::QueueType::Graphics;
  }
  enc.queue_type_ = queue_type;

  if (!enc.binder_pools_[enc.curr_frame_i_].pool) {
    for (size_t i = 0; i < frames_in_flight(); i++) {
      enc.binder_pools_[i].init(*this);
    }
    auto& binder = enc.binder_;
    binder.writes.reserve(120);
    binder.img_infos.reserve(120);
    binder.buf_infos.reserve(120);
  } else {
    enc.binder_pools_[enc.curr_frame_i_].reset(*this);
  }

  VkCommandBufferBeginInfo begin_info{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
  VK_CHECK(vkResetCommandBuffer(enc.cmd_bufs_[frame_idx()], 0));
  VK_CHECK(vkBeginCommandBuffer(enc.cmd_bufs_[frame_idx()], &begin_info));
  set_vk_debug_name(
      VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)enc.cmd_bufs_[frame_idx()],
      ("cmd_buf_" + std::to_string(frame_idx()) + "_" + std::to_string(curr_cmd_encoder_i_))
          .c_str());
  curr_cmd_encoder_i_++;
  return &enc;
}

rhi::PipelineHandle VulkanDevice::create_graphics_pipeline(
    const rhi::GraphicsPipelineCreateInfo& info) {
  std::array<VkPipelineShaderStageCreateInfo, 3> stages;
  uint32_t module_count = 0;
  std::vector<VkPushConstantRange> push_constant_ranges;
  DescSetCreateInfo set0_info;
  std::vector<BindlessBindingUsage> merged_bindless;
  std::vector<VkDescriptorSetLayoutBinding> merged_bindings;

  for (size_t i = 0; i < info.shaders.size(); i++, module_count++) {
    const auto& shader_info = info.shaders[i];
    if (shader_info.type == rhi::ShaderType::None) {
      break;
    }

    auto spirv_code =
        read_file_to_uint_vec(get_spv_path(shader_lib_dir_, shader_info.path, shader_info.type));
    std::vector<BindlessBindingUsage> shader_bindless;
    reflect_shader(spirv_code, convert_shader_stage(shader_info.type), push_constant_ranges,
                   set0_info, shader_bindless);
    merge_bindless_reflection(merged_bindless, shader_bindless);

    VkShaderModule module = create_shader_module(spirv_code);
    stages[i] = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = convert_shader_stage(shader_info.type),
        .module = module,
        .pName = info.shaders[i].entry_point.c_str(),
    };
  }

  VkPushConstantRange merged_push_constant_range{};
  bool has_range = false;
  for (auto& range : push_constant_ranges) {
    if (!has_range) {
      merged_push_constant_range = range;
      has_range = true;
    } else {
      merged_push_constant_range.stageFlags |= range.stageFlags;
      ASSERT(merged_push_constant_range.offset == range.offset);
      merged_push_constant_range.size = std::max(merged_push_constant_range.size, range.size);
    }
  }

  for (const auto& b : set0_info.bindings) {
    bool exists = false;
    for (auto& existing_b : merged_bindings) {
      if (existing_b.binding == b.binding) {
        existing_b.stageFlags |= b.stageFlags;
        ASSERT(existing_b.descriptorCount == b.descriptorCount);
        ASSERT(existing_b.descriptorType == b.descriptorType);
        exists = true;
        break;
      }
    }
    if (!exists) {
      merged_bindings.emplace_back(b);
    }
  }

  std::ranges::sort(merged_bindings,
                    [](const VkDescriptorSetLayoutBinding& a,
                       const VkDescriptorSetLayoutBinding& b) { return a.binding < b.binding; });

  VkPushConstantRange* pc_ptr = has_range ? &merged_push_constant_range : nullptr;
  uint32_t pc_count = has_range ? 1u : 0u;
  CachedPipelineLayout cached_pl =
      get_or_create_pipeline_layout(merged_bindings, merged_bindless, pc_ptr, pc_count);
  VkPipelineLayout pipeline_layout = cached_pl.layout;
  VkDescriptorSetLayout set_layout = cached_pl.set0_layout;

  std::array<VkPipelineColorBlendAttachmentState, 10> attachments{};

  uint32_t i = 0;
  uint32_t color_format_cnt = info.rendering.color_formats.size();
  uint32_t attachment_cnt = info.blend.attachments.size();
  for (const auto& attachment : info.blend.attachments) {
    attachments[i++] = convert_color_blend_attachment(attachment);
  }
  VkPipelineColorBlendStateCreateInfo blend_state{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable = info.blend.logic_op_enable,
      .logicOp = convert_logic_op(info.blend.logic_op),
      .attachmentCount = color_format_cnt,
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
      .depthCompareOp = convert_compare_op(info.depth_stencil.depth_compare_op),
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

  gch::small_vector<VkDynamicState, 10> states;
  states.emplace_back(VK_DYNAMIC_STATE_VIEWPORT);
  states.emplace_back(VK_DYNAMIC_STATE_SCISSOR);
  states.emplace_back(VK_DYNAMIC_STATE_FRONT_FACE);
  states.emplace_back(VK_DYNAMIC_STATE_CULL_MODE);
  states.emplace_back(VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE);
  states.emplace_back(VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE);
  states.emplace_back(VK_DYNAMIC_STATE_DEPTH_COMPARE_OP);

  // check if mesh shader
  bool is_mesh_shader = false;
  for (const auto& shader : info.shaders) {
    if (shader.type == rhi::ShaderType::Task || shader.type == rhi::ShaderType::Mesh) {
      is_mesh_shader = true;
      break;
    }
  }

  if (!is_mesh_shader) {
    states.emplace_back(VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY);
  }

  dynamic_state.dynamicStateCount = static_cast<uint32_t>(states.size());
  dynamic_state.pDynamicStates = states.data();

  VkPipelineVertexInputStateCreateInfo vertex_state{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo input_assembly{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = convert_prim_topology(info.topology),
  };

  gch::small_vector<VkFormat, rhi::k_max_color_attachments> color_formats;
  for (auto format : info.rendering.color_formats) {
    if (format != rhi::TextureFormat::Undefined) {
      color_formats.push_back(convert_format(format));
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
      .layout = pipeline_layout,
  };

  VkPipeline vk_pipeline;
  VK_CHECK(vkCreateGraphicsPipelines(device_, nullptr, 1, &cinfo, nullptr, &vk_pipeline));

  for (uint32_t i = 0; i < module_count; i++) {
    vkDestroyShaderModule(device_, stages[i].module, nullptr);
  }
  VkShaderStageFlags push_stages = has_range ? merged_push_constant_range.stageFlags : 0u;
  return pipeline_pool_.alloc(
      info, vk_pipeline, pipeline_layout, set_layout, std::move(merged_bindings),
      rhi::compute_render_target_info_hash(info.rendering), cached_pl.bindless_first_set,
      std::move(cached_pl.bindless_sets), push_stages);
}

void VulkanDevice::submit_frame() {
  ZoneScoped;
  // submit queues
  for (size_t cmd_enc_i = 0; cmd_enc_i < curr_cmd_encoder_i_; cmd_enc_i++) {
    auto& enc = *cmd_encoders_[cmd_enc_i];
    ASSERT(enc.queue_type_ != rhi::QueueType::Copy);
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
      ZoneScopedN("vkWaitForFences");
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
    uint32_t bi = buf->raw_bindless_idx();
    if (bi != rhi::k_invalid_bindless_idx && bi != 0u) {
      free_bindless_storage_idx(bi);
    }
    del_q_.enqueue({buf->buffer_, buf->allocation_});
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
    // Metal asserts child views are destroyed before the texture; Vulkan cleans them up here.
    for (auto& tv : tex->tex_views) {
      if (tv.view == VK_NULL_HANDLE) {
        continue;
      }
      uint32_t vbi = tv.bindless_idx;
      if (vbi != rhi::k_invalid_bindless_idx && vbi != 0u) {
        clear_bindless_image_slot(vbi);
        free_bindless_image_slot(vbi);
      }
      del_q_.enqueue(tv.view);
      tv = {};
    }
    tex->tex_views.clear();

    uint32_t bi = tex->raw_bindless_idx();
    if (bi != rhi::k_invalid_bindless_idx && bi != 0u) {
      clear_bindless_image_slot(bi);
      free_bindless_image_slot(bi);
    }
    if (!tex->is_swapchain_image_) {
      del_q_.enqueue({tex->image_, tex->allocation_});
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
    uint32_t bi = sampler->raw_bindless_idx();
    if (bi != rhi::k_invalid_bindless_idx && bi != 0u) {
      write_bindless_sampler(bi, null_bindless_sampler_);
      free_bindless_sampler_idx(bi);
    }
    del_q_.enqueue(sampler->sampler_);
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

void VulkanDevice::enqueue_swapchain_for_present(rhi::Swapchain* swapchain,
                                                 rhi::CmdEncoder* cmd_enc) {
  auto* vk_enc = (VulkanCmdEncoder*)cmd_enc;
  vk_enc->submit_swapchains_.emplace_back(swapchain);
}

void VulkanDevice::begin_swapchain_rendering(rhi::Swapchain* swapchain, rhi::CmdEncoder* cmd_enc,
                                             glm::vec4* clear_color) {
  enqueue_swapchain_for_present(swapchain, cmd_enc);
  cmd_enc->begin_rendering({
      rhi::RenderAttInfo{
          .image = swapchain->get_current_texture(),
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
  ZoneScoped;
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

namespace {

std::string to_string(VkPresentModeKHR present_mode) {
  switch (present_mode) {
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
      return "IMMEDIATE";
    case VK_PRESENT_MODE_MAILBOX_KHR:
      return "MAILBOX";
    case VK_PRESENT_MODE_FIFO_KHR:
      return "FIFO";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
      return "FIFO_RELAXED";
    case VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR:
      return "SHARED_DEMAND_REFRESH";
    case VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR:
      return "SHARED_CONTINUOUS_REFRESH";
    default:
      return "UNKNOWN";
  }
  return "UNKNOWN";
}

}  // namespace

void VulkanDevice::cmd_encoder_wait_for(rhi::CmdEncoder* /*cmd_enc_first*/,
                                        rhi::CmdEncoder* /*cmd_enc_second*/) {}

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
  auto vsync = desc.vsync;
  VkPresentModeKHR preferred_present_mode{vsync ? VK_PRESENT_MODE_MAILBOX_KHR
                                                : VK_PRESENT_MODE_IMMEDIATE_KHR};
  for (uint32_t i = 0; i < present_mode_count; i++) {
    if (present_modes[i] == preferred_present_mode) {
      selected_present_mode = preferred_present_mode;
      break;
    }
  }
  LINFO("selected_present_mode: {}", to_string(selected_present_mode));

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
  rhi::TextureUsage usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::TransferSrc |
                            rhi::TextureUsage::TransferDst;
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
    VkImage swapchain_images[k_max_swapchain_images];
    ASSERT(swapchain_image_count <= k_max_swapchain_images);
    VK_CHECK(vkGetSwapchainImagesKHR(device_, swapchain->swapchain_, &swapchain_image_count,
                                     swapchain_images));
    rhi::TextureDesc tex_desc{
        .format = convert_format(selected_format),
        .usage = usage,
        .dims = {swap_info.imageExtent.width, swap_info.imageExtent.height, 1},
        .mip_levels = 1,
        .array_length = 1,
        .flags = rhi::TextureDescFlags::NoBindless | rhi::TextureDescFlags::DisableCPUAccessOnUMA,
    };
    swapchain->desc_.width = swap_info.imageExtent.width;
    swapchain->desc_.height = swap_info.imageExtent.height;
    for (uint32_t i = 0; i < swapchain_image_count; i++) {
      ASSERT(i < (uint32_t)ARRAY_SIZE(swapchain->textures_));
      if (swapchain->textures_[i].is_valid()) {
        destroy(swapchain->textures_[i]);
      }
      auto tex_handle = texture_pool_.alloc(tex_desc, rhi::k_invalid_bindless_idx,
                                            swapchain_images[i], VmaAllocation{}, true);
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
      ASSERT(i < (uint32_t)ARRAY_SIZE(swapchain->acquire_semaphores_));
      swapchain->acquire_semaphores_[i] =
          create_semaphore(("swapchain_acquire_semaphore_" + std::to_string(i)).c_str());
    }
    for (VkSemaphore sem : swapchain->ready_to_present_semaphores_) {
      if (sem) del_q_.enqueue(sem);
    }
    for (uint32_t i = 0; i < swapchain_image_count; i++) {
      ASSERT(i < (uint32_t)ARRAY_SIZE(swapchain->ready_to_present_semaphores_));
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
  auto spirv_code =
      read_file_to_uint_vec(get_spv_path(shader_lib_dir_, cinfo.path, rhi::ShaderType::Compute));

  std::vector<VkPushConstantRange> push_constant_ranges;
  DescSetCreateInfo set0_cinfo;
  std::vector<BindlessBindingUsage> shader_bindless;
  reflect_shader(spirv_code, VK_SHADER_STAGE_COMPUTE_BIT, push_constant_ranges, set0_cinfo,
                 shader_bindless);

  CachedPipelineLayout cached_pl = get_or_create_pipeline_layout(
      set0_cinfo.bindings, shader_bindless,
      push_constant_ranges.empty() ? nullptr : push_constant_ranges.data(),
      static_cast<uint32_t>(push_constant_ranges.size()));

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
      .layout = cached_pl.layout,
  };

  VkPipeline vk_pipeline;
  VK_CHECK(vkCreateComputePipelines(device_, nullptr, 1, &pipeline_cinfo, nullptr, &vk_pipeline));

  vkDestroyShaderModule(device_, module, nullptr);

  VkShaderStageFlags push_stages = 0;
  for (const auto& r : push_constant_ranges) {
    push_stages |= r.stageFlags;
  }
  return pipeline_pool_.alloc(cinfo, vk_pipeline, cached_pl.layout, cached_pl.set0_layout,
                              std::move(set0_cinfo.bindings), cached_pl.bindless_first_set,
                              std::move(cached_pl.bindless_sets), push_stages);
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

uint64_t VulkanDevice::hash_pipeline_layout_cinfo(const VkPipelineLayoutCreateInfo& cinfo) {
  uint64_t hash = 1;
  for (uint32_t i = 0; i < cinfo.setLayoutCount; i++) {
    util::hash::hash_combine(hash, (uint64_t)cinfo.pSetLayouts[i]);
  }
  for (uint32_t i = 0; i < cinfo.pushConstantRangeCount; i++) {
    const auto& pc_range = cinfo.pPushConstantRanges[i];
    util::hash::hash_combine(hash, pc_range.offset);
    util::hash::hash_combine(hash, pc_range.size);
    util::hash::hash_combine(hash, pc_range.stageFlags);
  }
  return hash;
}

void VulkanDevice::merge_bindless_reflection(std::vector<BindlessBindingUsage>& dst,
                                             const std::vector<BindlessBindingUsage>& src) {
  dst.resize(std::max(dst.size(), src.size()));
  for (size_t i = 0; i < src.size(); ++i) {
    if (!src[i].used) {
      continue;
    }
    if (!dst[i].used) {
      dst[i] = src[i];
      continue;
    }
    if (dst[i].binding.descriptorType != src[i].binding.descriptorType) {
      dst[i] = src[i];
    } else {
      dst[i].binding.stageFlags |= src[i].binding.stageFlags;
    }
  }
}

void VulkanDevice::init_uab_heap(BindlessHeap& heap, VkDescriptorType type, uint32_t count) {
  heap.capacity = count;
  ALWAYS_ASSERT(count >= 8u);
  VkDescriptorPoolSize ps{.type = type, .descriptorCount = count};
  VkDescriptorPoolCreateInfo pc{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
                                .maxSets = 1,
                                .poolSizeCount = 1,
                                .pPoolSizes = &ps};
  VK_CHECK(vkCreateDescriptorPool(device_, &pc, nullptr, &heap.pool));

  VkDescriptorSetLayoutBinding binding{.binding = 0,
                                       .descriptorType = type,
                                       .descriptorCount = count,
                                       .stageFlags = VK_SHADER_STAGE_ALL,
                                       .pImmutableSamplers = nullptr};
  VkDescriptorBindingFlags bf =
      VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
  VkDescriptorSetLayoutBindingFlagsCreateInfo bfc{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
      .bindingCount = 1,
      .pBindingFlags = &bf,
  };
  VkDescriptorSetLayoutCreateInfo lc{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext = &bfc,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
      .bindingCount = 1,
      .pBindings = &binding};
  VK_CHECK(vkCreateDescriptorSetLayout(device_, &lc, nullptr, &heap.layout));

  VkDescriptorSetAllocateInfo ac{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                 .descriptorPool = heap.pool,
                                 .descriptorSetCount = 1,
                                 .pSetLayouts = &heap.layout};
  VK_CHECK(vkAllocateDescriptorSets(device_, &ac, &heap.set));

  heap.freelist.reserve(count);
  for (uint32_t i = 0; i < count; i++) {
    heap.freelist.push_back(count - 1u - i);
  }
}

void VulkanDevice::shutdown_uab_heap(BindlessHeap& heap) {
  if (heap.pool) {
    vkDestroyDescriptorPool(device_, heap.pool, nullptr);
    heap.pool = VK_NULL_HANDLE;
  }
  if (heap.layout) {
    vkDestroyDescriptorSetLayout(device_, heap.layout, nullptr);
    heap.layout = VK_NULL_HANDLE;
  }
  heap.set = VK_NULL_HANDLE;
  heap.freelist.clear();
  heap.capacity = 0;
}

int VulkanDevice::alloc_uab_heap_slot(BindlessHeap& heap) {
  std::lock_guard lock(heap.mutex);
  if (heap.freelist.empty()) {
    return -1;
  }
  uint32_t idx = heap.freelist.back();
  heap.freelist.pop_back();
  return static_cast<int>(idx);
}

void VulkanDevice::free_uab_heap_slot(BindlessHeap& heap, uint32_t idx) {
  std::lock_guard lock(heap.mutex);
  heap.freelist.push_back(idx);
}

void VulkanDevice::init_bindless_heaps() {
  VkPhysicalDeviceDescriptorIndexingProperties indexing_props{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES,
  };
  VkPhysicalDeviceProperties2 props2{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                                     .pNext = &indexing_props};
  vkGetPhysicalDeviceProperties2(physical_device_, &props2);

  VkBufferCreateInfo null_buf_info{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                   .size = 256,
                                   .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
  VmaAllocationCreateInfo null_alloc{};
  null_alloc.usage = VMA_MEMORY_USAGE_AUTO;
  VK_CHECK(vmaCreateBuffer(allocator_, &null_buf_info, &null_alloc, &null_storage_buffer_,
                           &null_storage_buffer_alloc_, nullptr));

  uint32_t storage_cap = indexing_props.maxDescriptorSetUpdateAfterBindStorageBuffers / 4u;
  storage_cap = std::min(storage_cap, 500000u);
  ALWAYS_ASSERT(storage_cap >= 64u);
  bindless_storage_capacity_ = storage_cap;

  VkDescriptorPoolSize pool_size{.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                 .descriptorCount = bindless_storage_capacity_};
  VkDescriptorPoolCreateInfo pool_cinfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
                                        .maxSets = 1,
                                        .poolSizeCount = 1,
                                        .pPoolSizes = &pool_size};
  VK_CHECK(vkCreateDescriptorPool(device_, &pool_cinfo, nullptr, &bindless_storage_pool_));

  VkDescriptorSetLayoutBinding storage_binding{.binding = 0,
                                               .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                               .descriptorCount = bindless_storage_capacity_,
                                               .stageFlags = VK_SHADER_STAGE_ALL,
                                               .pImmutableSamplers = nullptr};
  VkDescriptorBindingFlags bind_flags =
      VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
  VkDescriptorSetLayoutBindingFlagsCreateInfo bind_flags_cinfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
      .bindingCount = 1,
      .pBindingFlags = &bind_flags,
  };
  VkDescriptorSetLayoutCreateInfo storage_layout_cinfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext = &bind_flags_cinfo,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
      .bindingCount = 1,
      .pBindings = &storage_binding,
  };
  VK_CHECK(vkCreateDescriptorSetLayout(device_, &storage_layout_cinfo, nullptr,
                                       &bindless_storage_layout_));

  VkDescriptorSetAllocateInfo alloc_cinfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                          .descriptorPool = bindless_storage_pool_,
                                          .descriptorSetCount = 1,
                                          .pSetLayouts = &bindless_storage_layout_};
  VK_CHECK(vkAllocateDescriptorSets(device_, &alloc_cinfo, &bindless_storage_set_));

  bindless_storage_freelist_.reserve(bindless_storage_capacity_);
  for (uint32_t i = 0; i < bindless_storage_capacity_; i++) {
    bindless_storage_freelist_.push_back(bindless_storage_capacity_ - 1u - i);
  }
  int reserved_storage0 = alloc_bindless_storage_idx();
  ALWAYS_ASSERT(reserved_storage0 == 0);
  VkDescriptorBufferInfo null_storage_info{
      .buffer = null_storage_buffer_, .offset = 0, .range = VK_WHOLE_SIZE};
  VkWriteDescriptorSet null_storage_write{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                          .dstSet = bindless_storage_set_,
                                          .dstBinding = 0,
                                          .dstArrayElement = 0,
                                          .descriptorCount = 1,
                                          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                          .pBufferInfo = &null_storage_info};
  vkUpdateDescriptorSets(device_, 1, &null_storage_write, 0, nullptr);

  VkBufferCreateInfo null_texel_buf_info{
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = 4096,
      .usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT};
  VK_CHECK(vmaCreateBuffer(allocator_, &null_texel_buf_info, &null_alloc, &null_texel_buffer_,
                           &null_texel_buffer_alloc_, nullptr));

  VkBufferViewCreateInfo utbv{.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
                              .buffer = null_texel_buffer_,
                              .format = VK_FORMAT_R32_UINT,
                              .offset = 0,
                              .range = VK_WHOLE_SIZE};
  VK_CHECK(vkCreateBufferView(device_, &utbv, nullptr, &null_uniform_texel_view_));
  VkBufferViewCreateInfo stbv{.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
                              .buffer = null_texel_buffer_,
                              .format = VK_FORMAT_R32_UINT,
                              .offset = 0,
                              .range = VK_WHOLE_SIZE};
  VK_CHECK(vkCreateBufferView(device_, &stbv, nullptr, &null_storage_texel_view_));

  VkImageCreateInfo null_img{.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                             .imageType = VK_IMAGE_TYPE_2D,
                             .format = VK_FORMAT_R8G8B8A8_UNORM,
                             .extent = {1, 1, 1},
                             .mipLevels = 1,
                             .arrayLayers = 1,
                             .samples = VK_SAMPLE_COUNT_1_BIT,
                             .tiling = VK_IMAGE_TILING_OPTIMAL,
                             .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                      VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                             .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
  VK_CHECK(vmaCreateImage(allocator_, &null_img, &null_alloc, &null_image_, &null_image_alloc_,
                          nullptr));
  VkImageViewCreateInfo null_iv{.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                .image = null_image_,
                                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                .format = VK_FORMAT_R8G8B8A8_UNORM,
                                .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                     .baseMipLevel = 0,
                                                     .levelCount = 1,
                                                     .baseArrayLayer = 0,
                                                     .layerCount = 1}};
  VK_CHECK(vkCreateImageView(device_, &null_iv, nullptr, &null_image_view_));

  null_bindless_sampler_ = create_vk_sampler({
      .min_filter = rhi::FilterMode::Nearest,
      .mag_filter = rhi::FilterMode::Nearest,
      .mipmap_mode = rhi::FilterMode::Nearest,
      .address_mode = rhi::AddressMode::ClampToEdge,
      .flags = rhi::SamplerDescFlags::NoBindless,
  });

  auto cap_texel =
      std::min(500000u, indexing_props.maxDescriptorSetUpdateAfterBindSampledImages / 4u);
  cap_texel = std::max(cap_texel, 64u);
  auto cap_sampled =
      std::min(500000u, indexing_props.maxDescriptorSetUpdateAfterBindSampledImages / 4u);
  cap_sampled = std::max(cap_sampled, 64u);
  auto cap_storage_img =
      std::min(500000u, indexing_props.maxDescriptorSetUpdateAfterBindStorageImages / 4u);
  cap_storage_img = std::max(cap_storage_img, 64u);
  uint32_t cap_sampler = indexing_props.maxDescriptorSetUpdateAfterBindSamplers;
  cap_sampler = std::min(std::max(cap_sampler, 256u), 4096u);

  init_uab_heap(bindless_uniform_texel_, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, cap_texel);
  init_uab_heap(bindless_storage_texel_, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, cap_texel);
  const uint32_t cap_bindless_img = std::min(cap_sampled, cap_storage_img);
  init_uab_heap(bindless_sampled_image_, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, cap_bindless_img);
  init_uab_heap(bindless_storage_image_, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, cap_bindless_img);
  init_uab_heap(bindless_sampler_, VK_DESCRIPTOR_TYPE_SAMPLER, cap_sampler);

  auto prime_heap_slot0 = [&](BindlessHeap& heap, auto&& write_null) {
    int z = alloc_uab_heap_slot(heap);
    ALWAYS_ASSERT(z == 0);
    write_null();
  };

  prime_heap_slot0(bindless_uniform_texel_,
                   [&] { write_bindless_uniform_texel(0, null_uniform_texel_view_); });
  prime_heap_slot0(bindless_storage_texel_,
                   [&] { write_bindless_storage_texel(0, null_storage_texel_view_); });
  prime_heap_slot0(bindless_sampled_image_, [&] {
    write_bindless_sampled_image(0, null_image_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  });
  prime_heap_slot0(bindless_storage_image_, [&] {
    write_bindless_storage_image(0, null_image_view_, VK_IMAGE_LAYOUT_GENERAL);
  });
  // prime_heap_slot0(bindless_sampler_, [&] { write_bindless_sampler(0, null_bindless_sampler_);
  // });

  VkDescriptorPoolSize pad_pool_size{.type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 8};
  VkDescriptorPoolCreateInfo pad_pool_cinfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                            .maxSets = 64,
                                            .poolSizeCount = 1,
                                            .pPoolSizes = &pad_pool_size};
  VK_CHECK(vkCreateDescriptorPool(device_, &pad_pool_cinfo, nullptr, &padding_descriptor_pool_));

  VkDescriptorSetLayoutCreateInfo empty_layout_cinfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 0,
      .pBindings = nullptr,
  };
  VK_CHECK(vkCreateDescriptorSetLayout(device_, &empty_layout_cinfo, nullptr,
                                       &empty_descriptor_set_layout_));
  VkDescriptorSetAllocateInfo empty_alloc{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                          .descriptorPool = padding_descriptor_pool_,
                                          .descriptorSetCount = 1,
                                          .pSetLayouts = &empty_descriptor_set_layout_};
  VK_CHECK(vkAllocateDescriptorSets(device_, &empty_alloc, &empty_descriptor_set_));
}

void VulkanDevice::shutdown_bindless_heaps() {
  if (padding_descriptor_pool_) {
    vkDestroyDescriptorPool(device_, padding_descriptor_pool_, nullptr);
    padding_descriptor_pool_ = VK_NULL_HANDLE;
  }
  if (empty_descriptor_set_layout_) {
    vkDestroyDescriptorSetLayout(device_, empty_descriptor_set_layout_, nullptr);
    empty_descriptor_set_layout_ = VK_NULL_HANDLE;
  }
  empty_descriptor_set_ = VK_NULL_HANDLE;

  shutdown_uab_heap(bindless_uniform_texel_);
  shutdown_uab_heap(bindless_storage_texel_);
  shutdown_uab_heap(bindless_sampled_image_);
  shutdown_uab_heap(bindless_storage_image_);
  shutdown_uab_heap(bindless_sampler_);

  if (null_bindless_sampler_) {
    vkDestroySampler(device_, null_bindless_sampler_, nullptr);
    null_bindless_sampler_ = VK_NULL_HANDLE;
  }
  if (null_image_view_) {
    vkDestroyImageView(device_, null_image_view_, nullptr);
    null_image_view_ = VK_NULL_HANDLE;
  }
  if (null_image_) {
    vmaDestroyImage(allocator_, null_image_, null_image_alloc_);
    null_image_ = VK_NULL_HANDLE;
    null_image_alloc_ = VK_NULL_HANDLE;
  }
  if (null_uniform_texel_view_) {
    vkDestroyBufferView(device_, null_uniform_texel_view_, nullptr);
    null_uniform_texel_view_ = VK_NULL_HANDLE;
  }
  if (null_storage_texel_view_) {
    vkDestroyBufferView(device_, null_storage_texel_view_, nullptr);
    null_storage_texel_view_ = VK_NULL_HANDLE;
  }
  if (null_texel_buffer_) {
    vmaDestroyBuffer(allocator_, null_texel_buffer_, null_texel_buffer_alloc_);
    null_texel_buffer_ = VK_NULL_HANDLE;
    null_texel_buffer_alloc_ = VK_NULL_HANDLE;
  }

  if (bindless_storage_pool_) {
    vkDestroyDescriptorPool(device_, bindless_storage_pool_, nullptr);
    bindless_storage_pool_ = VK_NULL_HANDLE;
  }
  if (bindless_storage_layout_) {
    vkDestroyDescriptorSetLayout(device_, bindless_storage_layout_, nullptr);
    bindless_storage_layout_ = VK_NULL_HANDLE;
  }
  bindless_storage_set_ = VK_NULL_HANDLE;
  bindless_storage_freelist_.clear();
  bindless_storage_capacity_ = 0;

  if (null_storage_buffer_) {
    vmaDestroyBuffer(allocator_, null_storage_buffer_, null_storage_buffer_alloc_);
    null_storage_buffer_ = VK_NULL_HANDLE;
    null_storage_buffer_alloc_ = VK_NULL_HANDLE;
  }
}

int VulkanDevice::alloc_bindless_storage_idx() {
  std::lock_guard lock(bindless_storage_mutex_);
  if (bindless_storage_freelist_.empty()) {
    return -1;
  }
  uint32_t idx = bindless_storage_freelist_.back();
  bindless_storage_freelist_.pop_back();
  return static_cast<int>(idx);
}

void VulkanDevice::free_bindless_storage_idx(uint32_t idx) {
  std::lock_guard lock(bindless_storage_mutex_);
  bindless_storage_freelist_.push_back(idx);
}

void VulkanDevice::write_bindless_storage_descriptor(uint32_t idx, VkBuffer buffer) {
  VkDescriptorBufferInfo buf_info{.buffer = buffer, .offset = 0, .range = VK_WHOLE_SIZE};
  VkWriteDescriptorSet write{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                             .dstSet = bindless_storage_set_,
                             .dstBinding = 0,
                             .dstArrayElement = idx,
                             .descriptorCount = 1,
                             .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                             .pBufferInfo = &buf_info};
  vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
}

int VulkanDevice::alloc_bindless_image_slot() {
  std::scoped_lock lock(bindless_sampled_image_.mutex, bindless_storage_image_.mutex);
  if (bindless_sampled_image_.freelist.empty() || bindless_storage_image_.freelist.empty()) {
    return -1;
  }
  uint32_t si = bindless_sampled_image_.freelist.back();
  uint32_t st = bindless_storage_image_.freelist.back();
  ALWAYS_ASSERT(si == st);
  bindless_sampled_image_.freelist.pop_back();
  bindless_storage_image_.freelist.pop_back();
  return static_cast<int>(si);
}

void VulkanDevice::free_bindless_image_slot(uint32_t idx) {
  std::scoped_lock lock(bindless_sampled_image_.mutex, bindless_storage_image_.mutex);
  bindless_sampled_image_.freelist.push_back(idx);
  bindless_storage_image_.freelist.push_back(idx);
}

void VulkanDevice::clear_bindless_image_slot(uint32_t idx) {
  write_bindless_sampled_image(idx, null_image_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  write_bindless_storage_image(idx, null_image_view_, VK_IMAGE_LAYOUT_GENERAL);
}

void VulkanDevice::write_bindless_uniform_texel(uint32_t idx, VkBufferView view) {
  VkWriteDescriptorSet w{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                         .dstSet = bindless_uniform_texel_.set,
                         .dstBinding = 0,
                         .dstArrayElement = idx,
                         .descriptorCount = 1,
                         .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
                         .pTexelBufferView = &view};
  vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
}

void VulkanDevice::write_bindless_storage_texel(uint32_t idx, VkBufferView view) {
  VkWriteDescriptorSet w{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                         .dstSet = bindless_storage_texel_.set,
                         .dstBinding = 0,
                         .dstArrayElement = idx,
                         .descriptorCount = 1,
                         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
                         .pTexelBufferView = &view};
  vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
}

void VulkanDevice::write_bindless_sampled_image(uint32_t idx, VkImageView view,
                                                VkImageLayout layout) {
  VkDescriptorImageInfo ii{.sampler = VK_NULL_HANDLE, .imageView = view, .imageLayout = layout};
  VkWriteDescriptorSet w{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                         .dstSet = bindless_sampled_image_.set,
                         .dstBinding = 0,
                         .dstArrayElement = idx,
                         .descriptorCount = 1,
                         .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                         .pImageInfo = &ii};
  vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
}

void VulkanDevice::write_bindless_storage_image(uint32_t idx, VkImageView view,
                                                VkImageLayout layout) {
  VkDescriptorImageInfo ii{.sampler = VK_NULL_HANDLE, .imageView = view, .imageLayout = layout};
  VkWriteDescriptorSet w{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                         .dstSet = bindless_storage_image_.set,
                         .dstBinding = 0,
                         .dstArrayElement = idx,
                         .descriptorCount = 1,
                         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                         .pImageInfo = &ii};
  vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
}

int VulkanDevice::alloc_bindless_sampler_idx() { return alloc_uab_heap_slot(bindless_sampler_); }

void VulkanDevice::free_bindless_sampler_idx(uint32_t idx) {
  free_uab_heap_slot(bindless_sampler_, idx);
}

void VulkanDevice::write_bindless_sampler(uint32_t idx, VkSampler sampler) {
  VkDescriptorImageInfo ii{
      .sampler = sampler, .imageView = VK_NULL_HANDLE, .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED};
  VkWriteDescriptorSet w{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                         .dstSet = bindless_sampler_.set,
                         .dstBinding = 0,
                         .dstArrayElement = idx,
                         .descriptorCount = 1,
                         .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                         .pImageInfo = &ii};
  vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
}

VulkanDevice::CachedPipelineLayout VulkanDevice::get_or_create_pipeline_layout(
    const std::vector<VkDescriptorSetLayoutBinding>& merged_set0,
    const std::vector<BindlessBindingUsage>& merged_bindless, VkPushConstantRange* pc_ranges,
    uint32_t pc_range_count) {
  VkDescriptorSetLayout set0_layout{};
  VkDescriptorSetLayoutCreateInfo set0_cinfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = static_cast<uint32_t>(merged_set0.size()),
      .pBindings = merged_set0.data()};
  uint64_t set0_hash = hash_descriptor_set_layout_cinfo(set0_cinfo);
  auto set0_it = set_layout_cache_.find(set0_hash);
  if (set0_it != set_layout_cache_.end()) {
    set0_layout = set0_it->second;
  } else {
    VK_CHECK(vkCreateDescriptorSetLayout(device_, &set0_cinfo, nullptr, &set0_layout));
    set_layout_cache_[set0_hash] = set0_layout;
  }

  std::vector<VkDescriptorSetLayout> all_layouts;
  std::vector<VkDescriptorSet> bindless_handles;
  all_layouts.push_back(set0_layout);
  uint32_t bindless_first_set = 0;
  if (!merged_bindless.empty()) {
    bindless_first_set = 1;
    for (const auto& slot : merged_bindless) {
      if (slot.used) {
        switch (slot.binding.descriptorType) {
          case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            all_layouts.push_back(bindless_storage_layout_);
            bindless_handles.push_back(bindless_storage_set_);
            break;
          case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            all_layouts.push_back(bindless_uniform_texel_.layout);
            bindless_handles.push_back(bindless_uniform_texel_.set);
            break;
          case VK_DESCRIPTOR_TYPE_SAMPLER:
            all_layouts.push_back(bindless_sampler_.layout);
            bindless_handles.push_back(bindless_sampler_.set);
            break;
          case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            all_layouts.push_back(bindless_sampled_image_.layout);
            bindless_handles.push_back(bindless_sampled_image_.set);
            break;
          case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            all_layouts.push_back(bindless_storage_image_.layout);
            bindless_handles.push_back(bindless_storage_image_.set);
            break;
          case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            all_layouts.push_back(bindless_storage_texel_.layout);
            bindless_handles.push_back(bindless_storage_texel_.set);
            break;
          case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
            LCRITICAL("Vulkan bindless: acceleration structure heap not implemented");
            exit(1);
          default:
            LCRITICAL("Vulkan bindless: unsupported descriptor type for set > 0 (implement heap)");
            exit(1);
        }
      } else {
        all_layouts.push_back(empty_descriptor_set_layout_);
        bindless_handles.push_back(empty_descriptor_set_);
      }
    }
  }

  VkPipelineLayoutCreateInfo pl_cinfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                      .setLayoutCount = static_cast<uint32_t>(all_layouts.size()),
                                      .pSetLayouts = all_layouts.data(),
                                      .pushConstantRangeCount = pc_range_count,
                                      .pPushConstantRanges = pc_ranges};
  uint64_t pl_hash = hash_pipeline_layout_cinfo(pl_cinfo);
  auto pl_it = pipeline_layout_cache_.find(pl_hash);
  if (pl_it != pipeline_layout_cache_.end()) {
    return pl_it->second;
  }

  VkPipelineLayout pipeline_layout{};
  VK_CHECK(vkCreatePipelineLayout(device_, &pl_cinfo, nullptr, &pipeline_layout));
  CachedPipelineLayout entry{.layout = pipeline_layout,
                             .set0_layout = set0_layout,
                             .bindless_first_set = bindless_first_set,
                             .bindless_sets = std::move(bindless_handles)};
  pipeline_layout_cache_[pl_hash] = entry;
  return pipeline_layout_cache_[pl_hash];
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

void VulkanDevice::reflect_shader(std::span<const uint32_t> spirv_code, VkShaderStageFlagBits stage,
                                  std::vector<VkPushConstantRange>& out_pc_ranges,
                                  DescSetCreateInfo& out_set_0_info,
                                  std::vector<BindlessBindingUsage>& out_shader_bindless) {
  spv_reflect::ShaderModule refl{spirv_code.size() * sizeof(uint32_t), spirv_code.data(),
                                 SPV_REFLECT_MODULE_FLAG_NO_COPY};
  auto result = refl.GetResult();
  ASSERT(result == SPV_REFLECT_RESULT_SUCCESS);
  uint32_t set_count = 0;
  refl.EnumerateDescriptorSets(&set_count, nullptr);
  std::vector<SpvReflectDescriptorSet*> sets(set_count);
  if (set_count > 0) {
    refl.EnumerateDescriptorSets(&set_count, sets.data());
  }

  uint32_t pc_count = 0;
  refl.EnumeratePushConstantBlocks(&pc_count, nullptr);
  std::vector<SpvReflectBlockVariable*> push_constants(pc_count);
  if (pc_count > 0) {
    refl.EnumeratePushConstantBlocks(&pc_count, push_constants.data());
  }

  for (uint32_t i = 0; i < pc_count; i++) {
    auto& pc = push_constants[i];
    out_pc_ranges.emplace_back(VkPushConstantRange{
        .stageFlags = stage,
        .offset = pc->offset,
        .size = pc->size,
    });
  }

  const auto refl_stage = (VkShaderStageFlagBits)refl.GetShaderStage();
  for (uint32_t i = 0; i < set_count; i++) {
    auto* set = sets[i];
    if (set->set == 0) {
      for (uint32_t j = 0; j < set->binding_count; j++) {
        SpvReflectDescriptorBinding* binding = set->bindings[j];
        auto& layout_b = out_set_0_info.bindings.emplace_back(VkDescriptorSetLayoutBinding{
            .binding = binding->binding,
            .descriptorType = (VkDescriptorType)binding->descriptor_type,
            .descriptorCount = binding->count,
            .stageFlags = refl_stage,
            .pImmutableSamplers = nullptr,
        });
        if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER &&
            binding->binding >= 100) {
          layout_b.pImmutableSamplers = immutable_samplers_.data();
        }
      }
    } else {
      for (uint32_t j = 0; j < set->binding_count; j++) {
        SpvReflectDescriptorBinding* binding = set->bindings[j];
        out_shader_bindless.resize(
            std::max(out_shader_bindless.size(), static_cast<size_t>(set->set)));
        BindlessBindingUsage& slot = out_shader_bindless[set->set - 1];
        if (!slot.used) {
          slot.used = true;
          slot.binding.binding = binding->binding;
          slot.binding.descriptorType = (VkDescriptorType)binding->descriptor_type;
          slot.binding.descriptorCount = binding->count;
          slot.binding.stageFlags = refl_stage;
          slot.binding.pImmutableSamplers = nullptr;
        } else {
          ASSERT(slot.binding.binding == binding->binding);
          ASSERT(slot.binding.descriptorCount == binding->count);
          ASSERT(slot.binding.descriptorType == (VkDescriptorType)binding->descriptor_type);
          slot.binding.stageFlags |= refl_stage;
        }
      }
    }
  }
}

VkSampler VulkanDevice::create_vk_sampler(const rhi::SamplerDesc& desc) {
  VkSamplerCreateInfo cinfo{
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = convert_filter_mode(desc.mag_filter),
      .minFilter = convert_filter_mode(desc.min_filter),
      .mipmapMode = convert_mipmap_mode(desc.mipmap_mode),
      .addressModeU = convert_address_mode(desc.address_mode),
      .addressModeV = convert_address_mode(desc.address_mode),
      .addressModeW = convert_address_mode(desc.address_mode),
      .anisotropyEnable = desc.anisotropy_enable,
      .maxAnisotropy = desc.max_anisotropy,
      .compareEnable = desc.compare_enable,
      .compareOp = convert_compare_op(desc.compare_op),
      .minLod = desc.min_lod,
      .maxLod = desc.max_lod,
      .borderColor = convert_border_color(desc.border_color),
  };

  VkSampler sampler;
  VK_CHECK(vkCreateSampler(device_, &cinfo, nullptr, &sampler));
  ASSERT(sampler);
  return sampler;
}

void VulkanDevice::acquire_next_swapchain_image(rhi::Swapchain* swapchain) {
  // acquire next swapchain image
  auto* swap = (VulkanSwapchain*)swapchain;
  swap->acquire_semaphore_idx_ = (swap->acquire_semaphore_idx_ + 1) % swap->swapchain_tex_count_;

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
}

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
