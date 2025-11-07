#include "MetalCmdEncoder.hpp"

#include <Metal/Metal.hpp>

#include "core/EAssert.hpp"
#include "gfx/GFXTypes.hpp"
#include "gfx/metal/MetalDevice.hpp"
#include "gfx/metal/MetalUtil.hpp"

namespace {
struct Cbuffer2 {
  uint32_t draw_id;
  uint32_t vertex_id_base;
};
constexpr size_t k_pc_size = 160 + sizeof(Cbuffer2);

using namespace rhi;

// bool stage_is_compute_encoder(rhi::PipelineStage stage) {
//   return stage & (rhi::PipelineStage_ComputeShader | rhi::PipelineStage_AllCommands |
//                   rhi::PipelineStage_AllTransfer);
// }

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
  render_enc_->barrierAfterQueueStages(MTL::StageAll, MTL::StageAll, MTL4::VisibilityOptionNone);

  render_enc_->setArgumentTable(arg_table_, MTL::RenderStageFragment | MTL::RenderStageVertex);

  desc->release();
  if (depth_desc) {
    depth_desc->release();
  }
}

MetalCmdEncoder::MetalCmdEncoder(MetalDevice* device, MTL4::CommandBuffer* cmd_buf)
    : device_(device), cmd_buf_(cmd_buf) {
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
  ASSERT(size <= k_pc_size);
  auto [pc_buf, pc_buf_offset] = device_->push_constant_allocator_->alloc(k_pc_size);
  memcpy((uint8_t*)pc_buf->contents() + pc_buf_offset, data, size);
  // auto [tlab_buf, arg_buf_offset] = device_->arg_buf_allocator_->alloc(sizeof(TLAB));
  // auto* tlab = (TLAB*)((uint8_t*)tlab_buf->contents() + arg_buf_offset);
  // tlab->push_constant_buf = pc_buf->gpuAddress() + pc_buf_offset;
  //
  // tlab_buf_ = tlab_buf->gpuAddress() + arg_buf_offset;
  // tlab_size_ = sizeof(TLAB);
  pc_buf_ = pc_buf->gpuAddress() + pc_buf_offset;
  pc_buf_size_ = k_pc_size;

  // arg_table_->setAddress(tlab_buf_, 2);
}

void MetalCmdEncoder::draw_indexed_primitives(rhi::PrimitiveTopology topology,
                                              rhi::BufferHandle index_buf, size_t index_start,
                                              size_t count, size_t instance_count,
                                              size_t base_vertex, size_t base_instance) {
  auto* buf = device_->get_mtl_buf(index_buf);

  // MTL::PrimitiveType primitiveType, N
  //  indexCount,
  //  MTL::IndexType indexType,
  //  MTL::GPUAddress indexBuffer,
  //  NS::UInteger indexBufferLength,
  //  NS::UInteger instanceCount,
  //  NS::Integer baseVertex,
  //  NS::UInteger baseInstance)
  render_enc_->drawIndexedPrimitives(mtl::util::convert(topology), count, MTL::IndexTypeUInt32,
                                     buf->gpuAddress() + index_start, buf->length(), instance_count,
                                     base_vertex, base_instance);
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

void MetalCmdEncoder::upload_texture_data(rhi::BufferHandle src_buf, size_t src_offset,
                                          size_t src_bytes_per_row, rhi::TextureHandle dst_tex) {
  end_render_encoder();
  start_compute_encoder();

  auto* buf = device_->get_mtl_buf(src_buf);
  auto* tex = device_->get_mtl_tex(dst_tex);
  ALWAYS_ASSERT(buf);
  ALWAYS_ASSERT(tex);
  MTL::Size img_size = MTL::Size::Make(tex->width(), tex->height(), tex->depth());
  ALWAYS_ASSERT(img_size.width * img_size.depth * img_size.height * 4 <=
                device_->get_buf(src_buf)->size());
  compute_enc_->copyFromBuffer(buf, src_offset, src_bytes_per_row, 0, img_size, tex, 0, 0,
                               MTL::Origin::Make(0, 0, 0));
  compute_enc_->barrierAfterEncoderStages(MTL::StageBlit, MTL::StageBlit,
                                          MTL4::VisibilityOptionDevice);
  if (tex->mipmapLevelCount() > 1) {
    compute_enc_->generateMipmaps(tex);
  }
  compute_enc_->barrierAfterEncoderStages(MTL::StageBlit, MTL::StageBlit,
                                          MTL4::VisibilityOptionDevice);
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

void MetalCmdEncoder::start_compute_encoder() {
  if (!compute_enc_) {
    compute_enc_ = cmd_buf_->computeCommandEncoder();
    // TODO: diabolical
    compute_enc_->barrierAfterQueueStages(MTL::StageAll, MTL::StageAll,
                                          MTL4::VisibilityOptionDevice);
  }
}

void MetalCmdEncoder::prepare_indexed_indirect_draws(rhi::BufferHandle indirect_buf, size_t offset,
                                                     size_t draw_cnt, rhi::BufferHandle index_buf,
                                                     size_t index_buf_offset) {
  curr_bound_index_buf_ = index_buf;
  curr_bound_index_buf_offset_ = index_buf_offset;

  end_render_encoder();
  start_compute_encoder();
  ASSERT(device_->dispatch_indirect_pso_);

  compute_enc_->barrierAfterStages(MTL::StageAll, MTL::StageAll, MTL4::VisibilityOptionDevice);

  compute_enc_->setComputePipelineState(device_->dispatch_indirect_pso_);
  compute_enc_->setArgumentTable(arg_table_);

  arg_table_->setAddress(pc_buf_, 0);

  struct Args2 {
    uint32_t draw_cnt;
  };

  auto [args2_buf, args2_offset] = device_->test_allocator_->alloc(sizeof(Args2));
  auto* args2 = (Args2*)((uint8_t*)args2_buf->contents() + args2_offset);
  args2->draw_cnt = draw_cnt;
  arg_table_->setAddress(args2_buf->gpuAddress() + args2_offset, 1);

  arg_table_->setAddress(device_->get_mtl_buf(device_->main_icb_container_buf_)->gpuAddress(), 2);

  auto [out_pc_arg_buf, out_pc_arg_buf_offset] =
      device_->test_allocator_->alloc(draw_cnt * sizeof(TLAB));
  arg_table_->setAddress(out_pc_arg_buf->gpuAddress() + out_pc_arg_buf_offset, 3);

  auto* indirect_buffer = device_->get_mtl_buf(indirect_buf);
  ASSERT(indirect_buffer);
  arg_table_->setAddress(indirect_buffer->gpuAddress() + offset, 4);

  ASSERT(curr_bound_index_buf_.is_valid());
  auto* index_buffer = device_->get_mtl_buf(curr_bound_index_buf_);
  ASSERT(index_buffer);
  arg_table_->setAddress(index_buffer->gpuAddress() + curr_bound_index_buf_offset_, 5);

  arg_table_->setAddress(device_->get_mtl_buf(device_->resource_descriptor_table_)->gpuAddress(),
                         6);
  arg_table_->setAddress(device_->get_mtl_buf(device_->sampler_descriptor_table_)->gpuAddress(), 7);

  uint32_t threads_per_tg_x = 32;
  uint32_t tg_x = (draw_cnt + threads_per_tg_x - 1) / threads_per_tg_x;
  compute_enc_->dispatchThreadgroups(MTL::Size::Make(tg_x, 1, 1),
                                     MTL::Size::Make(threads_per_tg_x, 1, 1));
}

void MetalCmdEncoder::barrier(rhi::PipelineStage, rhi::AccessFlags, rhi::PipelineStage,
                              rhi::AccessFlags) {
  ASSERT(compute_enc_ || render_enc_);
  // bool src_is_compute = stage_is_compute_encoder(src_stage);
  // bool dst_is_compute = stage_is_compute_encoder(dst_stage);
  if (compute_enc_) {
    compute_enc_->barrierAfterEncoderStages(MTL::StageAll, MTL::StageAll,
                                            MTL4::VisibilityOptionDevice);
    compute_enc_->barrierAfterQueueStages(MTL::StageAll, MTL::StageAll,
                                          MTL4::VisibilityOptionDevice);
    compute_enc_->barrierAfterStages(MTL::StageAll, MTL::StageAll, MTL4::VisibilityOptionDevice);
  } else if (render_enc_) {
    render_enc_->barrierAfterStages(MTL::StageAll, MTL::StageAll, MTL4::VisibilityOptionDevice);
    render_enc_->barrierAfterQueueStages(MTL::StageAll, MTL::StageAll,
                                         MTL4::VisibilityOptionDevice);
  }
}

void MetalCmdEncoder::draw_indexed_indirect(rhi::BufferHandle, size_t, size_t draw_cnt) {
  ASSERT(render_enc_);
  render_enc_->executeCommandsInBuffer(device_->main_icb_, NS::Range::Make(0, draw_cnt));
}

void MetalCmdEncoder::copy_tex_to_buf(rhi::TextureHandle src_tex, size_t src_slice,
                                      size_t src_level, rhi::BufferHandle dst_buf,
                                      size_t dst_offset) {
  end_render_encoder();
  start_compute_encoder();
  auto* tex = device_->get_mtl_tex(src_tex);
  auto* buf = device_->get_mtl_buf(dst_buf);
  size_t bytes_per_row = tex->width() * 4;
  compute_enc_->copyFromTexture(tex, src_slice, src_level, MTL::Origin::Make(0, 0, 0),
                                MTL::Size::Make(tex->width(), tex->height(), tex->depth()), buf,
                                dst_offset, bytes_per_row, 0);
}
