#include "MetalCmdEncoderICBMgr.hpp"

#include <metal/Metal.hpp>

#include "core/Config.hpp"
#include "gfx/metal/MetalDevice.hpp"

namespace TENG_NAMESPACE {

void MetalCmdEncoderICBMgr::init_icb_arg_encoder_and_buf_and_set_icb(
    std::span<MTL::IndirectCommandBuffer*> icbs, size_t i) {
  auto encode_icb = [this, i, icbs]() {
    main_icb_container_arg_enc_->setArgumentBuffer(device_->get_mtl_buf(main_icb_container_buf_[i]),
                                                   0);
    main_icb_container_arg_enc_->setIndirectCommandBuffer(icbs[i], 0);
  };
  if (!main_icb_container_arg_enc_) {
    MTL::ArgumentDescriptor* arg = MTL::ArgumentDescriptor::alloc()->init();
    arg->setIndex(0);
    arg->setAccess(MTL::BindingAccessReadWrite);
    arg->setDataType(MTL::DataTypeIndirectCommandBuffer);
    std::array<NS::Object*, 1> args_arr{arg};
    const NS::Array* args = NS::Array::array(args_arr.data(), args_arr.size());
    main_icb_container_arg_enc_ = device_->get_device()->newArgumentEncoder(args);
    arg->release();
  }
  if (main_icb_container_buf_.empty()) {
    for (size_t j = 0; j < k_max_frames_in_flight; j++) {
      main_icb_container_buf_.emplace_back(device_->create_buf_h(
          {.size = align_up(main_icb_container_arg_enc_->encodedLength(), 256)}));
    }
  }
  encode_icb();
}

MetalCmdEncoderICBMgr::~MetalCmdEncoderICBMgr() {
  if (main_icb_container_arg_enc_) {
    main_icb_container_arg_enc_->release();
  }
}

MTL::Buffer* MetalCmdEncoderICBMgr::get_icb(size_t i) {
  ASSERT(i < main_icb_container_buf_.size());
  return device_->get_mtl_buf(main_icb_container_buf_[i]);
}

}  // namespace TENG_NAMESPACE
