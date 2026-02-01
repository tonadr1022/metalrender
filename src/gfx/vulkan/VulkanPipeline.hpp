#pragma once

#include "gfx/Pipeline.hpp"

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

namespace gfx::vk {

class VulkanPipeline : public rhi::Pipeline {
 public:
  VulkanPipeline() = default;
  ~VulkanPipeline() override = default;
};

}  // namespace gfx::vk

} // namespace TENG_NAMESPACE
