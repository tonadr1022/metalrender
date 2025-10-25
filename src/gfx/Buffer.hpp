#pragma once

#include <cstddef>

#include "GFXTypes.hpp"

namespace rhi {

class Buffer {
 public:
  explicit Buffer(const BufferDesc& desc) : desc_(desc) {}
  Buffer() = default;
  ~Buffer() = default;
  virtual void* contents() = 0;
  [[nodiscard]] size_t size() const { return desc_.size; }
  [[nodiscard]] const BufferDesc& desc() const { return desc_; }

 private:
  BufferDesc desc_;
};

}  // namespace rhi
