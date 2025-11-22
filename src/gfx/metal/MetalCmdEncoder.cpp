#include "MetalCmdEncoder.hpp"

// clang-format off
#include <Metal/Metal.hpp>
#define IR_RUNTIME_METALCPP
#include <metal_irconverter_runtime/metal_irconverter_runtime_wrapper.h>
// clang-format on

#include <Metal/MTLCommandEncoder.hpp>

#include "core/EAssert.hpp"
#include "gfx/CmdEncoder.hpp"
#include "gfx/GFXTypes.hpp"
#include "gfx/metal/MetalCmdEncoderCommon.hpp"
#include "gfx/metal/MetalDevice.hpp"
#include "gfx/metal/MetalUtil.hpp"

using namespace gfx::mtl;

namespace {

MTL::Stages convert_stage(rhi::PipelineStage stage) {
  MTL::Stages result{};
  if (stage & (rhi::PipelineStage_AllCommands)) {
    result |= MTL::StageAll;
  }
  if (stage & (rhi::PipelineStage_FragmentShader | rhi::PipelineStage_EarlyFragmentTests |
               rhi::PipelineStage_LateFragmentTests | rhi::PipelineStage_ColorAttachmentOutput |
               rhi::PipelineStage_AllGraphics)) {
    result |= MTL::StageFragment;
  }
  if (stage & (rhi::PipelineStage_VertexShader | rhi::PipelineStage_VertexInput |
               rhi::PipelineStage_AllGraphics | rhi::PipelineStage_DrawIndirect)) {
    result |= MTL::StageVertex;
  }
  if (stage & (rhi::PipelineStage_ComputeShader)) {
    result |= MTL::StageDispatch;
  }
  if (stage & (rhi::PipelineStage_AllTransfer)) {
    result |= MTL::StageBlit;
  }
  return result;
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
  flush_render_barriers();

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
  vp.znear = 0.0f;
  vp.zfar = 1.f;
  render_enc_->setViewport(vp);
}

void MetalCmdEncoder::set_scissor(glm::uvec2 min, glm::uvec2 extent) {
  MTL::ScissorRect r{.x = min.x, .y = min.y, .width = extent.x, .height = extent.y};
  render_enc_->setScissorRect(r);
}

void MetalCmdEncoder::draw_primitives(rhi::PrimitiveTopology topology, size_t vertex_start,
                                      size_t count, size_t instance_count) {
  render_enc_->drawPrimitives(mtl::util::convert(topology), vertex_start, count, instance_count);
}

void MetalCmdEncoder::push_constants(void* data, size_t size) {
  ASSERT(size <= k_tlab_size);
  arg_table_->setAddress(device_->get_mtl_buf(device_->resource_descriptor_table_)->gpuAddress(),
                         kIRDescriptorHeapBindPoint);
  arg_table_->setAddress(device_->get_mtl_buf(device_->sampler_descriptor_table_)->gpuAddress(),
                         kIRSamplerHeapBindPoint);
  memcpy(pc_data_, data, size);
}

void MetalCmdEncoder::draw_indexed_primitives(rhi::PrimitiveTopology topology,
                                              rhi::BufferHandle index_buf, size_t index_start,
                                              size_t count, size_t instance_count,
                                              size_t base_vertex_idx, size_t base_instance,
                                              rhi::IndexType index_type) {
  auto* buf = device_->get_mtl_buf(index_buf);
  auto [pc_buf, pc_buf_offset] = device_->push_constant_allocator_->alloc(k_tlab_size);

  auto* tlab = reinterpret_cast<TLAB_Layout*>((uint8_t*)pc_buf->contents() + pc_buf_offset);
  memcpy(tlab->pc_data, pc_data_, k_pc_size);
  tlab->cbuffer2.draw_id = base_instance;
  tlab->cbuffer2.vertex_id_base = base_vertex_idx;
  arg_table_->setAddress(pc_buf->gpuAddress() + pc_buf_offset, kIRArgumentBufferBindPoint);

  render_enc_->drawIndexedPrimitives(
      mtl::util::convert(topology), count,
      index_type == rhi::IndexType::Uint32 ? MTL::IndexTypeUInt32 : MTL::IndexTypeUInt16,
      buf->gpuAddress() + index_start, buf->length(), instance_count, 0, 0);
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
  upload_texture_data(src_buf, src_offset, src_bytes_per_row, dst_tex,
                      device_->get_tex(dst_tex)->desc().dims, glm::uvec3{0, 0, 0});
}

void MetalCmdEncoder::upload_texture_data(rhi::BufferHandle src_buf, size_t src_offset,
                                          size_t src_bytes_per_row, rhi::TextureHandle dst_tex,
                                          glm::uvec3 src_size, glm::uvec3 dst_origin) {
  end_render_encoder();
  start_compute_encoder();
  auto* buf = device_->get_mtl_buf(src_buf);
  auto* tex = device_->get_mtl_tex(dst_tex);
  ALWAYS_ASSERT(buf);
  ALWAYS_ASSERT(tex);
  MTL::Size img_size = MTL::Size::Make(src_size.x, src_size.y, src_size.z);
  ALWAYS_ASSERT(img_size.width * img_size.depth * img_size.height * 4 <=
                device_->get_buf(src_buf)->size());
  compute_enc_->copyFromBuffer(buf, src_offset, src_bytes_per_row, 0, img_size, tex, 0, 0,
                               MTL::Origin::Make(dst_origin.x, dst_origin.y, dst_origin.z));
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
  }
}

uint32_t MetalCmdEncoder::prepare_indexed_indirect_draws(
    rhi::BufferHandle indirect_buf, size_t offset, size_t draw_cnt, rhi::BufferHandle index_buf,
    size_t index_buf_offset, void* push_constant_data, size_t push_constant_size) {
  auto [pc_buf, pc_buf_offset] = device_->push_constant_allocator_->alloc(k_tlab_size);
  memcpy((uint8_t*)pc_buf->contents() + pc_buf_offset, push_constant_data, push_constant_size);

  curr_bound_index_buf_ = index_buf;
  curr_bound_index_buf_offset_ = index_buf_offset;

  end_render_encoder();
  start_compute_encoder();
  ASSERT(device_->get_psos().dispatch_indirect_pso);
  ASSERT(device_->get_buf(indirect_buf)->desc().usage & rhi::BufferUsage_Indirect);

  auto [indirect_buf_id, icb] = device_->icb_mgr_.alloc(indirect_buf, draw_cnt);

  init_icb_arg_encoder_and_buf();
  main_icb_container_arg_enc_->setArgumentBuffer(device_->get_mtl_buf(main_icb_container_buf_), 0);
  main_icb_container_arg_enc_->setIndirectCommandBuffer(icb, 0);

  compute_enc_->setComputePipelineState(device_->get_psos().dispatch_indirect_pso);
  compute_enc_->setArgumentTable(arg_table_);

  arg_table_->setAddress(pc_buf->gpuAddress() + pc_buf_offset, 0);

  struct Args2 {
    uint32_t draw_cnt;
  };

  auto [args2_buf, args2_offset] = device_->test_allocator_->alloc(sizeof(Args2));
  auto* args2 = (Args2*)((uint8_t*)args2_buf->contents() + args2_offset);
  args2->draw_cnt = draw_cnt;
  arg_table_->setAddress(args2_buf->gpuAddress() + args2_offset, 1);

  arg_table_->setAddress(device_->get_mtl_buf(main_icb_container_buf_)->gpuAddress(), 2);

  auto [out_pc_arg_buf, out_pc_arg_buf_offset] =
      device_->test_allocator_->alloc(draw_cnt * sizeof(TLAB_Layout));
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
  return indirect_buf_id;
}

void MetalCmdEncoder::barrier(rhi::PipelineStage src_stage, rhi::AccessFlags,
                              rhi::PipelineStage dst_stage, rhi::AccessFlags) {
  auto src_mtl_stage = convert_stage(src_stage);
  auto dst_mtl_stage = convert_stage(dst_stage);
  if (dst_mtl_stage & (MTL::StageDispatch | MTL::StageBlit)) {
    compute_enc_flush_stages_ |= src_mtl_stage;
    compute_enc_dst_stages_ |= dst_mtl_stage;
  }
  if (dst_mtl_stage & (MTL::StageVertex | MTL::StageFragment | MTL::StageObject | MTL::StageMesh)) {
    render_enc_flush_stages_ |= src_mtl_stage;
    render_enc_dst_stages_ |= dst_mtl_stage;
  }
}

void MetalCmdEncoder::flush_compute_barriers() {
  if (compute_enc_ && compute_enc_flush_stages_) {
    compute_enc_->barrierAfterQueueStages(compute_enc_flush_stages_, compute_enc_dst_stages_,
                                          MTL4::VisibilityOptionDevice);
    compute_enc_flush_stages_ = 0;
    compute_enc_dst_stages_ = 0;
  }
}

void MetalCmdEncoder::flush_render_barriers() {
  if (render_enc_ && render_enc_flush_stages_) {
    render_enc_->barrierAfterQueueStages(render_enc_flush_stages_, render_enc_dst_stages_,
                                         MTL4::VisibilityOptionNone);
    render_enc_flush_stages_ = 0;
    render_enc_dst_stages_ = 0;
  }
}

void MetalCmdEncoder::draw_indexed_indirect(rhi::BufferHandle indirect_buf,
                                            uint32_t indirect_buf_id, size_t offset,
                                            size_t draw_cnt) {
  ASSERT(render_enc_);
  ALWAYS_ASSERT(offset == 0);
  render_enc_->executeCommandsInBuffer(device_->icb_mgr_.get(indirect_buf, indirect_buf_id),
                                       NS::Range::Make(0, draw_cnt));
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

void MetalCmdEncoder::init_icb_arg_encoder_and_buf() {
  if (main_icb_container_arg_enc_) {
    return;
  }
  MTL::ArgumentDescriptor* arg = MTL::ArgumentDescriptor::alloc()->init();
  arg->setIndex(0);
  arg->setAccess(MTL::BindingAccessReadWrite);
  arg->setDataType(MTL::DataTypeIndirectCommandBuffer);
  std::array<NS::Object*, 1> args_arr{arg};
  const NS::Array* args = NS::Array::array(args_arr.data(), args_arr.size());
  main_icb_container_arg_enc_ = device_->get_device()->newArgumentEncoder(args);
  main_icb_container_buf_ =
      device_->create_buf_h({.size = main_icb_container_arg_enc_->encodedLength()});
}

MetalCmdEncoder::~MetalCmdEncoder() {
  if (main_icb_container_arg_enc_) {
    main_icb_container_arg_enc_->release();
  }
}
