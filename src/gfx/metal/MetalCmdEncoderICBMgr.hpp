#pragma once

#include <span>
#include <vector>

#include "core/Config.hpp"
#include "gfx/rhi/GFXTypes.hpp"

namespace MTL {

class Buffer;
class ArgumentEncoder;
class IndirectCommandBuffer;
class ArgumentDescriptor;

}  // namespace MTL

namespace TENG_NAMESPACE {

namespace gfx::mtl {

class Device;

class CmdEncoderICBMgr {
 public:
  CmdEncoderICBMgr() = default;
  CmdEncoderICBMgr(const CmdEncoderICBMgr&) = delete;
  CmdEncoderICBMgr(CmdEncoderICBMgr&&) = delete;
  CmdEncoderICBMgr& operator=(const CmdEncoderICBMgr&) = delete;
  CmdEncoderICBMgr& operator=(CmdEncoderICBMgr&&) = delete;
  ~CmdEncoderICBMgr();

  void init(Device* device) { device_ = device; }
  void init_icb_arg_encoder_and_buf_and_set_icb(std::span<MTL::IndirectCommandBuffer*> icbs,
                                                size_t i);
  MTL::Buffer* get_icb(size_t i);

 private:
  std::vector<rhi::BufferHandleHolder> main_icb_container_buf_;
  MTL::ArgumentEncoder* main_icb_container_arg_enc_{};
  Device* device_{};
};

}  // namespace gfx::mtl

}  // namespace TENG_NAMESPACE
