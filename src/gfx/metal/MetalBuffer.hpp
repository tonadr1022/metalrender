#pragma once

#include <Metal/MTLResource.hpp>

#include "core/Config.hpp"
#include "gfx/rhi/Buffer.hpp"
#include "gfx/rhi/GFXTypes.hpp"

namespace MTL {
class Buffer;
}

namespace TENG_NAMESPACE {

namespace gfx::mtl {
class Buffer final : public rhi::Buffer {
 public:
  Buffer(const rhi::BufferDesc& desc, MTL::Buffer* buffer, MTL::ResourceOptions resource_options,
         uint32_t bindless_idx = rhi::k_invalid_bindless_idx);
  Buffer() = default;
  ~Buffer() = default;
  void* contents() override;
  [[nodiscard]] const void* contents() const override;
  [[nodiscard]] MTL::Buffer* buffer() { return buffer_; }

  [[nodiscard]] bool is_cpu_visible() const override {
    return !(resource_options_ & MTL::ResourceStorageModePrivate) &&
           !(resource_options_ & MTL::ResourceStorageModeManaged);
  }

 private:
  MTL::ResourceOptions resource_options_{};
  [[maybe_unused]] MTL::Buffer* buffer_{};
};

}  // namespace gfx::mtl

}  // namespace TENG_NAMESPACE
