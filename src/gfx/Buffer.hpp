#pragma once

#include <cstddef>

#include "GFXTypes.hpp"

namespace rhi {

class Buffer {
 public:
  explicit Buffer(const BufferDesc& desc, uint32_t gpu_slot = k_invalid_gpu_slot)
      : desc_(desc), gpu_slot_(gpu_slot) {}
  Buffer() = default;
  virtual void* contents() = 0;
  [[nodiscard]] size_t size() const { return desc_.size; }
  [[nodiscard]] const BufferDesc& desc() const { return desc_; }
  [[nodiscard]] uint32_t gpu_slot() const { return gpu_slot_; }

  constexpr static uint32_t k_invalid_gpu_slot = UINT32_MAX;

 private:
  BufferDesc desc_;
  uint32_t gpu_slot_{k_invalid_gpu_slot};
};

}  // namespace rhi
