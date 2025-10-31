#include "GPUFrameAllocator.hpp"

#include "Device.hpp"

rhi::Buffer* GPUFrameAllocator::get_buffer() { return device_->get_buf(buffers_[curr_frame_idx_]); }
