#pragma once

#include <cstddef>

namespace rhi {

struct BufferDesc {
  size_t size;
};

class Buffer {
 public:
  explicit Buffer(const BufferDesc& desc) : desc_(desc) {}

 private:
  [[maybe_unused]] BufferDesc desc_;
};

}  // namespace rhi
