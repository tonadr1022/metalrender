#include "GPUFrameAllocator.hpp"

#include "gfx/rhi/Device.hpp"

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

rhi::Buffer* GPUFrameAllocator::get_buffer() { return device_->get_buf(buffers_[curr_frame_idx_]); }

} // namespace TENG_NAMESPACE
