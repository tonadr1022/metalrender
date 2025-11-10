#include "VulkanDevice.hpp"

// clang-format off
#include <volk.h>
#include <VkBootstrap.h>

#include <print>
// clang-format on

#include "Window.hpp"
#include "core/Logger.hpp"

namespace {

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
    for (const auto& v : vkb_result.detailed_failure_reasons()) {
      LCRITICAL("\t{}", v);
    }
    exit(1);
  }
}

}  // namespace

void VulkanDevice::shutdown() {}

void VulkanDevice::init(const InitInfo& init_info) {
  vkb::InstanceBuilder builder;
  // init_info.window->
  if (init_info.validation_layers_enabled) {
  }
  auto inst_ret = builder.set_app_name(init_info.app_name.c_str())
                      .request_validation_layers(init_info.validation_layers_enabled)
                      .use_default_debug_messenger()
                      .set_debug_callback(vk_debug_callback)
                      .build();
  check_vkb_result("Failed to get vulkan instance", inst_ret);
  vkb::Instance vkb_inst = inst_ret.value();
  instance_ = vkb_inst.instance;

  vkb::PhysicalDeviceSelector phys_device_selector{vkb_inst};
  auto phys_ret = phys_device_selector.set_surface(surface_).set_minimum_version(1, 3).select();
  check_vkb_result("Failed to select physical device", phys_ret);
  physical_device_ = phys_ret.value().physical_device;

  vkb::DeviceBuilder device_builder{phys_ret.value()};
  auto device_ret = device_builder.build();
  check_vkb_result("Failed to create vulkan device", device_ret);

  const vkb::Device& vkb_device = device_ret.value();
  auto graphics_queue_ret = vkb_device.get_queue(vkb::QueueType::graphics);
  check_vkb_result("Failed to obtain graphics queue", graphics_queue_ret);
  graphics_queue_ = graphics_queue_ret.value();
  exit(1);
}
