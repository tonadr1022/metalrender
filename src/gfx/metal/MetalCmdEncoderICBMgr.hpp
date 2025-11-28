#pragma once

#include <span>
#include <vector>

#include "gfx/RendererTypes.hpp"

class MetalDevice;

namespace MTL {

class Buffer;
class ArgumentEncoder;
class IndirectCommandBuffer;
class ArgumentDescriptor;

}  // namespace MTL

class MetalCmdEncoderICBMgr {
 public:
  MetalCmdEncoderICBMgr() = default;
  MetalCmdEncoderICBMgr(const MetalCmdEncoderICBMgr&) = delete;
  MetalCmdEncoderICBMgr(MetalCmdEncoderICBMgr&&) = delete;
  MetalCmdEncoderICBMgr& operator=(const MetalCmdEncoderICBMgr&) = delete;
  MetalCmdEncoderICBMgr& operator=(MetalCmdEncoderICBMgr&&) = delete;
  ~MetalCmdEncoderICBMgr();

  explicit MetalCmdEncoderICBMgr(MetalDevice* device) : device_(device) {}
  void init_icb_arg_encoder_and_buf_and_set_icb(std::span<MTL::IndirectCommandBuffer*> icbs,
                                                size_t i);
  MTL::Buffer* get_icb(size_t i);

 private:
  std::vector<rhi::BufferHandleHolder> main_icb_container_buf_;
  MTL::ArgumentEncoder* main_icb_container_arg_enc_;
  MetalDevice* device_{};
};
