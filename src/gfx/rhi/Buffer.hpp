#pragma once

#include <cstddef>

#include "GFXTypes.hpp"
#include "core/Config.hpp"

namespace TENG_NAMESPACE {

namespace rhi {

class Buffer {
 public:
  Buffer() = default;
  ~Buffer() = default;
  virtual void* contents() = 0;
  [[nodiscard]] size_t size() const { return desc_.size; }
  [[nodiscard]] const BufferDesc& desc() const { return desc_; }
  [[nodiscard]] uint32_t bindless_idx() const {
    ASSERT(bindless_idx_ != k_invalid_bindless_idx);
    return bindless_idx_;
  }

  [[nodiscard]] virtual bool is_cpu_visible() const {
    return desc_.storage_mode == StorageMode::CPUAndGPU;
  }

 protected:
  explicit Buffer(const BufferDesc& desc, uint32_t bindless_idx = k_invalid_bindless_idx)
      : desc_(desc), bindless_idx_(bindless_idx) {}
  BufferDesc desc_;
  uint32_t bindless_idx_{k_invalid_bindless_idx};
};

}  // namespace rhi

}  // namespace TENG_NAMESPACE
