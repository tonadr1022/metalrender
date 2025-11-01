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

MTL::PrimitiveType convert(rhi::PrimitiveTopology top) {
  switch (top) {
    case rhi::PrimitiveTopology::TriangleList:
      return MTL::PrimitiveTypeTriangle;
    case rhi::PrimitiveTopology::TriangleStrip:
      return MTL::PrimitiveTypeTriangleStrip;
    case rhi::PrimitiveTopology::LineList:
      return MTL::PrimitiveTypeLine;
    case rhi::PrimitiveTopology::LineStrip:
      return MTL::PrimitiveTypeLineStrip;
    default:
      ALWAYS_ASSERT(0 && "unsupported primitive topology");
      return MTL::PrimitiveTypeTriangle;
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
      auto* tex = reinterpret_cast<MetalTexture*>(device_->get_tex(att.image));
      color_desc->setTexture(tex->texture());
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
    curr_render_enc_ = nullptr;
  }

  ASSERT(!curr_render_enc_);
  curr_render_enc_ = cmd_buf_->renderCommandEncoder(desc);

  curr_render_enc_->setArgumentTable(arg_table_, MTL::RenderStageFragment | MTL::RenderStageVertex);

  desc->release();
  if (depth_desc) {
    depth_desc->release();
  }
}

MetalCmdEncoder::MetalCmdEncoder(MetalDevice* device, MTL4::CommandBuffer* cmd_buf,
                                 MTL::ArgumentEncoder* top_level_arg_enc)
    : device_(device), cmd_buf_(cmd_buf), top_level_arg_enc_(top_level_arg_enc) {
  MTL4::ArgumentTableDescriptor* desc = MTL4::ArgumentTableDescriptor::alloc()->init();
  desc->setInitializeBindings(false);
  desc->setMaxBufferBindCount(10);
  desc->setMaxSamplerStateBindCount(10);
  desc->setMaxTextureBindCount(10);
  NS::Error* err{};
  arg_table_ = device_->get_device()->newArgumentTable(desc, &err);
}

void MetalCmdEncoder::end_encoding() {
  if (curr_render_enc_) {
    curr_render_enc_->endEncoding();
  }
  curr_render_enc_ = nullptr;
  if (curr_compute_enc_) {
    curr_compute_enc_->endEncoding();
  }
  curr_compute_enc_ = nullptr;
}

void MetalCmdEncoder::bind_pipeline(rhi::PipelineHandle handle) {
  auto* pipeline = reinterpret_cast<MetalPipeline*>(device_->get_pipeline(handle));
  ASSERT(pipeline);
  if (pipeline->render_pso) {
    ASSERT(curr_render_enc_);
    curr_render_enc_->setRenderPipelineState(pipeline->render_pso);
  } else if (pipeline->compute_pso) {
    ASSERT(curr_compute_enc_);
    curr_compute_enc_->setComputePipelineState(pipeline->compute_pso);
  } else {
    LERROR("invalid pipeline for MetalCmdEncoder::bind_pipeline");
    ASSERT(0);
  }
}

void MetalCmdEncoder::set_viewport(glm::uvec2 min, glm::uvec2 max) {
  // TODO: does y need flip? Is this necessary?
  MTL::Viewport vp;
  vp.originX = min.x;
  vp.originY = min.y;
  vp.width = max.x;
  vp.height = max.y;
  ASSERT(curr_render_enc_);
  curr_render_enc_->setViewport(vp);
}

void MetalCmdEncoder::draw_primitives(rhi::PrimitiveTopology topology, size_t vertex_start,
                                      size_t count, size_t instance_count) {
  ASSERT(curr_render_enc_);
  curr_render_enc_->drawPrimitives(convert(topology), vertex_start, count, instance_count);
}

void MetalCmdEncoder::push_constants(void* data, size_t size) {
  auto [pc_buf, pc_buf_offset] = device_->push_constant_allocator_->alloc(size);
  memcpy((uint8_t*)pc_buf->contents() + pc_buf_offset, data, size);
  auto [arg_buf, arg_buf_offset] = device_->alloc_arg_buf();
  struct TLAB {
    uint64_t pc_buf;
    uint64_t bdt;
  };
  auto* tlab = (TLAB*)((uint8_t*)arg_buf->contents() + arg_buf_offset);
  tlab->pc_buf = pc_buf->gpuAddress() + pc_buf_offset;
  tlab->bdt = device_->get_mtl_buf(device_->buffer_descriptor_table_)->gpuAddress();

  // TODO: magic num
  // top_level_arg_enc_->setArgumentBuffer(arg_buf, arg_buf_offset);
  // top_level_arg_enc_->setBuffer(pc_buf, pc_buf_offset, 0);
  // top_level_arg_enc_->setBuffer(device_->get_mtl_buf(device_->buffer_descriptor_table_), 0, 1);
  curr_arg_buf_ = arg_buf;
  curr_arg_buf_offset_ = arg_buf_offset;

  ASSERT(curr_arg_buf_->gpuAddress());
  arg_table_->setAddress(arg_buf->gpuAddress() + arg_buf_offset, 2);
}

void MetalCmdEncoder::draw_indexed_primitives(rhi::PrimitiveTopology topology,
                                              rhi::BufferHandle index_buf, size_t index_start,
                                              size_t count) {
  auto* buf = device_->get_mtl_buf(index_buf);
  curr_render_enc_->drawIndexedPrimitives(convert(topology), count, MTL::IndexTypeUInt32,
                                          buf->gpuAddress() + index_start, buf->length());
}
