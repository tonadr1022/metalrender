#include "MetalCmdEncoder.hpp"

#include <Metal/Metal.hpp>

#include "gfx/GFXTypes.hpp"
#include "gfx/metal/MetalDevice.hpp"
#include "gfx/metal/MetalUtil.hpp"

namespace {}  // namespace

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
      depth_desc->setLoadAction(mtl::util::convert(att.load_op));
      depth_desc->setStoreAction(mtl::util::convert(att.store_op));
      depth_desc->setClearDepth(att.clear_value.depth_stencil.depth);
      desc->setDepthAttachment(depth_desc);
    } else {  // color
      MTL::RenderPassColorAttachmentDescriptor* color_desc =
          desc->colorAttachments()->object(color_att_i);
      auto* tex = reinterpret_cast<MetalTexture*>(device_->get_tex(att.image));
      color_desc->setTexture(tex->texture());
      color_desc->setLoadAction(mtl::util::convert(att.load_op));
      color_desc->setStoreAction(mtl::util::convert(att.store_op));
      color_desc->setClearColor(
          MTL::ClearColor::Make(att.clear_value.color.r, att.clear_value.color.g,
                                att.clear_value.color.b, att.clear_value.color.a));
      color_att_i++;
    }
  }

  end_compute_encoder();

  ASSERT(!render_enc_);
  render_enc_ = cmd_buf_->renderCommandEncoder(desc);

  render_enc_->setArgumentTable(arg_table_, MTL::RenderStageFragment | MTL::RenderStageVertex);
  render_enc_->barrierAfterQueueStages(MTL::RenderStageFragment, MTL::RenderStageVertex,
                                       MTL4::VisibilityOptionDevice);

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
  if (render_enc_) {
    render_enc_->endEncoding();
    render_enc_ = nullptr;
  }
  if (compute_enc_) {
    compute_enc_->endEncoding();
    compute_enc_ = nullptr;
  }
}

void MetalCmdEncoder::bind_pipeline(rhi::PipelineHandle handle) {
  auto* pipeline = reinterpret_cast<MetalPipeline*>(device_->get_pipeline(handle));
  ASSERT(pipeline);
  if (pipeline->render_pso) {
    ASSERT(render_enc_);
    render_enc_->setRenderPipelineState(pipeline->render_pso);
  } else if (pipeline->compute_pso) {
    ASSERT(compute_enc_);
    compute_enc_->setComputePipelineState(pipeline->compute_pso);
  } else {
    LERROR("invalid pipeline for MetalCmdEncoder::bind_pipeline");
    ASSERT(0);
  }
}

void MetalCmdEncoder::set_viewport(glm::uvec2 min, glm::uvec2 extent) {
  MTL::Viewport vp;
  vp.originX = min.x;
  vp.originY = min.y;
  vp.width = extent.x;
  vp.height = extent.y;
  vp.znear = 0.00f;
  vp.zfar = 1.f;
  render_enc_->setViewport(vp);
}

void MetalCmdEncoder::draw_primitives(rhi::PrimitiveTopology topology, size_t vertex_start,
                                      size_t count, size_t instance_count) {
  render_enc_->drawPrimitives(mtl::util::convert(topology), vertex_start, count, instance_count);
}

void MetalCmdEncoder::push_constants(void* data, size_t size) {
  auto [pc_buf, pc_buf_offset] = device_->push_constant_allocator_->alloc(size);
  memcpy((uint8_t*)pc_buf->contents() + pc_buf_offset, data, size);
  auto [arg_buf, arg_buf_offset] = device_->alloc_arg_buf();
  struct TLAB {
    uint64_t push_constant_buf;
    uint64_t buffer_descriptor_table;
    uint64_t texture_descriptor_table;
    uint64_t sampler_descriptor_table;
  };
  auto* tlab = (TLAB*)((uint8_t*)arg_buf->contents() + arg_buf_offset);
  tlab->push_constant_buf = pc_buf->gpuAddress() + pc_buf_offset;
  tlab->buffer_descriptor_table =
      device_->get_mtl_buf(device_->buffer_descriptor_table_)->gpuAddress();
  tlab->texture_descriptor_table =
      device_->get_mtl_buf(device_->texture_descriptor_table_)->gpuAddress();
  tlab->sampler_descriptor_table =
      device_->get_mtl_buf(device_->sampler_descriptor_table_)->gpuAddress();

  curr_arg_buf_ = arg_buf;
  curr_arg_buf_offset_ = arg_buf_offset;

  ASSERT(curr_arg_buf_->gpuAddress());
  arg_table_->setAddress(arg_buf->gpuAddress() + arg_buf_offset, 2);
}

void MetalCmdEncoder::draw_indexed_primitives(rhi::PrimitiveTopology topology,
                                              rhi::BufferHandle index_buf, size_t index_start,
                                              size_t count) {
  auto* buf = device_->get_mtl_buf(index_buf);
  render_enc_->drawIndexedPrimitives(mtl::util::convert(topology), count, MTL::IndexTypeUInt32,
                                     buf->gpuAddress() + index_start, buf->length());
}

void MetalCmdEncoder::set_depth_stencil_state(rhi::CompareOp depth_compare_op,
                                              bool depth_write_enabled) {
  MTL::DepthStencilDescriptor* depth_stencil_desc = MTL::DepthStencilDescriptor::alloc()->init();
  depth_stencil_desc->setDepthCompareFunction(mtl::util::convert(depth_compare_op));
  depth_stencil_desc->setDepthWriteEnabled(depth_write_enabled);
  render_enc_->setDepthStencilState(
      device_->get_device()->newDepthStencilState(depth_stencil_desc));
}

void MetalCmdEncoder::set_cull_mode(rhi::CullMode cull_mode) {
  render_enc_->setCullMode(mtl::util::convert(cull_mode));
}

void MetalCmdEncoder::set_wind_order(rhi::WindOrder wind_order) {
  render_enc_->setFrontFacingWinding(mtl::util::convert(wind_order));
}

void MetalCmdEncoder::copy_buf_to_tex(rhi::BufferHandle src_buf, size_t src_offset,
                                      size_t src_bytes_per_row, rhi::TextureHandle dst_tex) {
  end_render_encoder();
  if (!compute_enc_) {
    compute_enc_ = cmd_buf_->computeCommandEncoder();
  }
  auto* buf = device_->get_mtl_buf(src_buf);
  auto* tex = device_->get_mtl_tex(dst_tex);
  MTL::Size img_size = MTL::Size::Make(tex->width(), tex->height(), tex->depth());
  compute_enc_->copyFromBuffer(buf, src_offset, src_bytes_per_row, 0, img_size, tex, 0, 0,
                               MTL::Origin::Make(0, 0, 0));
  compute_enc_->generateMipmaps(tex);
}

void MetalCmdEncoder::end_render_encoder() {
  if (render_enc_) {
    render_enc_->endEncoding();
    render_enc_ = nullptr;
  }
}

void MetalCmdEncoder::end_compute_encoder() {
  if (compute_enc_) {
    compute_enc_->endEncoding();
    compute_enc_ = nullptr;
  }
}
