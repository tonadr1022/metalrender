#pragma once

#include "core/BitUtil.hpp"
#include "gfx/rhi/Buffer.hpp"
#include "gfx/rhi/Config.hpp"

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

class GPUFrameAllocator;
namespace rhi {
class Device;
}

template <typename ElementT>
class PerFrameBuffer {
  friend class GPUFrameAllocator;
  PerFrameBuffer(GPUFrameAllocator& allocator, size_t offset_bytes, size_t element_count)
      : allocator_(allocator), element_count_(element_count), offset_bytes_(offset_bytes) {}

 public:
  [[nodiscard]] rhi::Buffer* get_buf() const;
  [[nodiscard]] size_t get_offset_bytes() const { return offset_bytes_; }
  void fill(const ElementT& data);

 private:
  GPUFrameAllocator& allocator_;
  size_t element_count_{};
  size_t offset_bytes_{};
};

class GPUFrameAllocator {
 public:
  GPUFrameAllocator(rhi::Device* device, size_t size, size_t frames_in_flight);
  void switch_to_next_buffer() { curr_frame_idx_ = (curr_frame_idx_ + 1) % frames_in_flight_; }

  rhi::Buffer* get_buffer();

  template <typename ElementT>
  PerFrameBuffer<ElementT> create_buffer(size_t element_count) {
    auto buf = PerFrameBuffer<ElementT>(*this, curr_alloc_offset_, element_count);
    curr_alloc_offset_ += util::align_256(element_count * sizeof(ElementT));
    return buf;
  }

 private:
  std::array<rhi::BufferHandleHolder, k_max_frames_in_flight> buffers_{};
  size_t curr_alloc_offset_{};
  size_t curr_frame_idx_{};
  size_t frames_in_flight_{};
  rhi::Device* device_{};
};

template <typename ElementT>
rhi::Buffer* PerFrameBuffer<ElementT>::get_buf() const {
  return allocator_.get_buffer();
}

template <typename ElementT>
void PerFrameBuffer<ElementT>::fill(const ElementT& data) {
  for (size_t i = 0; i < element_count_; i++) {
    *(reinterpret_cast<ElementT*>(reinterpret_cast<uint8_t*>(get_buf()->contents()) +
                                  offset_bytes_) +
      i) = data;
  }
}

} // namespace TENG_NAMESPACE
