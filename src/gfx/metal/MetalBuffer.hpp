#pragma once

#include "core/Config.hpp"
#include "gfx/rhi/Buffer.hpp"
#include "gfx/rhi/GFXTypes.hpp"

namespace MTL {
class Buffer;
}

namespace TENG_NAMESPACE {

class MetalBuffer final : public rhi::Buffer {
 public:
  MetalBuffer(const rhi::BufferDesc& desc, MTL::Buffer* buffer,
              uint32_t bindless_idx = rhi::k_invalid_bindless_idx);
  MetalBuffer() = default;
  ~MetalBuffer() = default;
  void* contents() override;
  [[nodiscard]] const void* contents() const override;
  [[nodiscard]] MTL::Buffer* buffer() { return buffer_; }

  [[nodiscard]] bool is_cpu_visible() const override {
    return desc_.storage_mode != rhi::StorageMode::GPUOnly;
  }

 private:
  [[maybe_unused]] MTL::Buffer* buffer_{};
};

}  // namespace TENG_NAMESPACE
