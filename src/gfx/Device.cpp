#include "Device.hpp"

#include "core/Logger.hpp"

extern std::unique_ptr<rhi::Device> create_vulkan_device();

#ifdef METAL_BACKEND
#include "gfx/metal/MetalDevice.hpp"
#endif

#ifdef VULKAN_BACKEND
#include "gfx/vulkan/VulkanDevice.hpp"
#endif

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
      return std::make_unique<VulkanDevice>();
#endif
  }
}

}  // namespace rhi
