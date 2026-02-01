#include "VulkanCommon.hpp"

#include <vulkan/vk_enum_string_helper.h>

#include "core/Logger.hpp"

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

namespace gfx::vk {

void print_vk_error(size_t x, bool exit_prog) {
  auto err = static_cast<VkResult>(x);
  if (err) {
    LERROR("Detected Vulkan error: {}", string_VkResult(err));
    if (exit_prog) {
      // exit(1);
    }
  }
}

}  // namespace gfx::vk

} // namespace TENG_NAMESPACE
