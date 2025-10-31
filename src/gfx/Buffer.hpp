#pragma once

#include <cstddef>

#include "GFXTypes.hpp"

namespace rhi {

class Buffer {
 public:
  Buffer() = default;
  ~Buffer() = default;
  virtual void* contents() = 0;
  [[nodiscard]] size_t size() const { return desc_.size; }
  [[nodiscard]] const BufferDesc& desc() const { return desc_; }
  [[nodiscard]] uint32_t bindless_idx() const { return bindless_idx_; }

 protected:
  explicit Buffer(const BufferDesc& desc, uint32_t bindless_idx = k_invalid_bindless_idx)
      : desc_(desc), bindless_idx_(bindless_idx) {}
  BufferDesc desc_;
  uint32_t bindless_idx_{k_invalid_bindless_idx};
};

}  // namespace rhi
