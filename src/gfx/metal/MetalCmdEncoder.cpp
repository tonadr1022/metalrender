#include "MetalCmdEncoder.hpp"

#include <Metal/MTLRenderPass.hpp>
#include <Metal/Metal.hpp>

#include "gfx/GFXTypes.hpp"
#include "gfx/metal/MetalDevice.hpp"

namespace {

MTL::LoadAction convert(rhi::LoadOp op) {
  switch (op) {
    case rhi::LoadOp::Load:
      return MTL::LoadActionLoad;
    case rhi::LoadOp::Clear:
      return MTL::LoadActionClear;
    default:
      return MTL::LoadActionDontCare;
  }
}

MTL::StoreAction convert(rhi::StoreOp op) {
  switch (op) {
    case rhi::StoreOp::Store:
      return MTL::StoreActionStore;
    default:
      return MTL::StoreActionDontCare;
  }
}

}  // namespace

void MetalCmdEncoder::begin_rendering(
    std::initializer_list<rhi::RenderingAttachmentInfo> attachments) {  // new command encoder
  MTL4::RenderPassDescriptor* desc = MTL4::RenderPassDescriptor::alloc()->init();
  size_t color_att_i{0};
  MTL::RenderPassDepthAttachmentDescriptor* depth_desc{};
  for (const auto& att : attachments) {
    // set depth and color atts
    if (att.type == rhi::RenderingAttachmentInfo::Type::DepthStencil) {
      depth_desc = MTL::RenderPassDepthAttachmentDescriptor::alloc()->init();
      depth_desc->setTexture(
          reinterpret_cast<MetalTexture*>(device_->get_tex(att.image))->texture());
      depth_desc->setLoadAction(convert(att.load_op));
      depth_desc->setStoreAction(convert(att.store_op));
      depth_desc->setClearDepth(att.clear_value.depth_stencil.depth);
      desc->setDepthAttachment(depth_desc);
    } else {  // color
      MTL::RenderPassColorAttachmentDescriptor* color_desc =
          desc->colorAttachments()->object(color_att_i);
      color_desc->setTexture(
          reinterpret_cast<MetalTexture*>(device_->get_tex(att.image))->texture());
      color_desc->setLoadAction(convert(att.load_op));
      color_desc->setStoreAction(convert(att.store_op));
      color_desc->setClearColor(
          MTL::ClearColor::Make(att.clear_value.color.r, att.clear_value.color.g,
                                att.clear_value.color.b, att.clear_value.color.a));
      color_att_i++;
    }
  }

  if (curr_compute_enc_) {
    curr_compute_enc_->endEncoding();
    curr_compute_enc_ = nullptr;
  }

  if (curr_render_enc_) {
    curr_render_enc_->endEncoding();
  }

  ASSERT(!curr_render_enc_);
  curr_render_enc_ = cmd_buf_->renderCommandEncoder(desc);

  desc->release();
  if (depth_desc) {
    depth_desc->release();
  }
}

MetalCmdEncoder::MetalCmdEncoder(MTL4::CommandBuffer* cmd_buf) : cmd_buf_(cmd_buf) {}

void MetalCmdEncoder::end_encoding() {
  if (curr_render_enc_) {
    curr_render_enc_->endEncoding();
  }
  if (curr_compute_enc_) {
    curr_compute_enc_->endEncoding();
  }
}

void MetalCmdEncoder::bind_pipeline(rhi::PipelineHandle handle) {
  auto* pipeline = reinterpret_cast<MetalPipeline*>(device_->get_pipeline(handle));
  if (!pipeline) {
    LERROR("pipeline not found");
    return;
  }

  if (pipeline->render_pso) {
    if (!curr_render_enc_) {
      LERROR("Cannot bind pipeline without active rendering commands");
      return;
    }
    curr_render_enc_->setRenderPipelineState(pipeline->render_pso);
  } else if (pipeline->compute_pso) {
    if (!curr_compute_enc_) {
      LERROR("Cannot bind pipeline without active compute");
      return;
    }
    curr_compute_enc_->setComputePipelineState(pipeline->compute_pso);
    LERROR("incorrect pipeline for MetalCmdEncoder::bind_pipeline. Need Graphics, have Compute");
  } else {
    LERROR("invalid pipeline for MetalCmdEncoder::bind_pipeline");
  }
}
