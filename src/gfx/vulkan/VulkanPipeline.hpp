#pragma once

#include "core/Config.hpp"
#include "gfx/Pipeline.hpp"

namespace TENG_NAMESPACE {

namespace gfx::vk {

class VulkanPipeline : public rhi::Pipeline {
 public:
  VulkanPipeline() = default;
  ~VulkanPipeline() override = default;
};

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
