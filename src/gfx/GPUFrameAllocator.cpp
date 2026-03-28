#include "GPUFrameAllocator.hpp"

#include "core/Config.hpp"
#include "gfx/rhi/Device.hpp"

namespace TENG_NAMESPACE {

namespace gfx {

rhi::Buffer* GPUFrameAllocator::get_buffer() { return device_->get_buf(buffers_[curr_frame_idx_]); }

}  // namespace gfx

}  // namespace TENG_NAMESPACE
