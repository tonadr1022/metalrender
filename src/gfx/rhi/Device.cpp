#include "Device.hpp"

#include "core/Logger.hpp"  // IWYU pragma: keep


#ifdef METAL_BACKEND
#include "gfx/metal/MetalDevice.hpp"
#endif

#ifdef VULKAN_BACKEND
#include "core/Config.hpp"
#include "gfx/vulkan/VulkanDevice.hpp"

extern std::unique_ptr<rhi::Device> create_vulkan_device();

#endif


namespace TENG_NAMESPACE {

namespace rhi {

std::unique_ptr<Device> create_device(GfxAPI api) {
  switch (api) {
    case rhi::GfxAPI::Metal:
#ifndef METAL_BACKEND
      LCRITICAL("Metal backend not available");
      exit(1);
      return nullptr;
#else
      return std::make_unique<MetalDevice>();
#endif
    case rhi::GfxAPI::Vulkan:
#ifndef VULKAN_BACKEND
      LCRITICAL("Vulkan backend not available");
      exit(1);
      return nullptr;
#else
      return std::make_unique<gfx::vk::VulkanDevice>();
#endif
  }
}

}  // namespace rhi

}  // namespace TENG_NAMESPACE
