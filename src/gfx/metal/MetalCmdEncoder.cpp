#include "MetalCmdEncoder.hpp"

#include <sys/wait.h>

// clang-format off
#include <Metal/MTL4Counters.hpp>
#include <Metal/MTLComputeCommandEncoder.hpp>
#include <Metal/Metal.hpp>
#include "gfx/metal/Config.hpp"
#include "gfx/metal/RootLayout.hpp"
#include "hlsl/shared_indirect.h"
#define IR_RUNTIME_METALCPP
#include <metal_irconverter_runtime/metal_irconverter_runtime_wrapper.h>
// clang-format on

#include <Metal/MTLCommandEncoder.hpp>

#include "core/EAssert.hpp"
#include "gfx/metal/MetalDevice.hpp"
#include "gfx/metal/MetalUtil.hpp"
#include "gfx/rhi/GFXTypes.hpp"

template <typename API>
typename API::RPDesc* create_render_pass(
    MetalDevice* device, const std::vector<rhi::RenderingAttachmentInfo>& attachments) {
  using RPDesc = typename API::RPDesc;
  using ColorDesc = typename API::ColorDesc;
  using DepthDesc = typename API::DepthDesc;

  RPDesc* desc = API::alloc_desc();
  size_t color_att_i = 0;

  DepthDesc* depth_desc = nullptr;

  for (const auto& att : attachments) {
    if (att.type == rhi::RenderingAttachmentInfo::Type::DepthStencil) {
      depth_desc = API::alloc_depth_desc();
      depth_desc->setTexture(
          reinterpret_cast<MetalTexture*>(device->get_tex(att.image))->texture());
      depth_desc->setLoadAction(mtl::util::convert(att.load_op));
      depth_desc->setStoreAction(mtl::util::convert(att.store_op));
      depth_desc->setClearDepth(att.clear_value.depth_stencil.depth);
      API::set_depth(desc, depth_desc);
    } else {
      ColorDesc* color_desc = API::get_color_desc(desc, color_att_i);
      auto* tex = reinterpret_cast<MetalTexture*>(device->get_tex(att.image));
      color_desc->setTexture(tex->texture());
      color_desc->setLoadAction(mtl::util::convert(att.load_op));
      color_desc->setStoreAction(mtl::util::convert(att.store_op));
      color_desc->setClearColor(
          MTL::ClearColor::Make(att.clear_value.color.r, att.clear_value.color.g,
                                att.clear_value.color.b, att.clear_value.color.a));
      color_att_i++;
    }
  }

  return desc;
}

enum EncoderType {
  EncoderType_Render = 1,
  EncoderType_Compute = 1 << 1,
  EncoderType_Blit = 1 << 2,
};

template MTL::RenderPassDescriptor* create_render_pass<MetalRenderPassAPI3>(
    MetalDevice*, const std::vector<rhi::RenderingAttachmentInfo>&);
template MTL4::RenderPassDescriptor* create_render_pass<MetalRenderPassAPI4>(
    MetalDevice*, const std::vector<rhi::RenderingAttachmentInfo>&);

template struct EncoderState<Metal3EncoderAPI>;
template struct EncoderState<Metal4EncoderAPI>;

namespace {

template <typename EncoderAPI, typename EncoderT>
void barrier_after_queue_stages(EncoderT& encoder, size_t src_stages, size_t dst_stages) {
  if constexpr (std::is_same_v<EncoderAPI, Metal4EncoderAPI>) {
    // TODO: Finer grained LMAO
    MTL4::VisibilityOptions visibility_options = MTL4::VisibilityOptionNone;
    encoder->barrierAfterStages(MTL::StageAll, MTL::StageAll, visibility_options);
    // encoder->barrierAfterQueueStages(src_stages, dst_stages, MTL4::VisibilityOptionDevice);
    if constexpr (std::is_same_v<EncoderT, MTL4::ComputeCommandEncoder*>) {
      encoder->barrierAfterStages(MTL::StageAll, MTL::StageAll, visibility_options);
      encoder->barrierAfterEncoderStages(
          MTL::StageDispatch | MTL::StageBlit | MTL::StageAccelerationStructure,
          MTL::StageDispatch | MTL::StageBlit | MTL::StageAccelerationStructure,
          visibility_options);
    } else if constexpr (std::is_same_v<EncoderT, MTL4::RenderCommandEncoder*>) {
      encoder->barrierAfterStages(MTL::StageAll, MTL::StageAll, visibility_options);
      encoder->barrierAfterEncoderStages(
          MTL::StageVertex | MTL::StageObject | MTL::StageMesh | MTL::StageFragment,
          MTL::StageVertex | MTL::StageObject | MTL::StageMesh | MTL::StageFragment,
          visibility_options);
    }
    encoder->barrierAfterQueueStages(src_stages, dst_stages, MTL4::VisibilityOptionDevice);
  } else {
    encoder->barrierAfterQueueStages(src_stages, dst_stages);
  }
}

enum EncoderSetBufferStage : uint32_t {
  Vertex = (1 << 0),
  Fragment = (1 << 1),
  Mesh = (1 << 2),
  Object = (1 << 3),
  Compute = (1 << 4)
};

template <typename EncoderAPI, typename EncoderT>
void set_buffer(EncoderState<EncoderAPI>& state, const EncoderT& encoder, uint32_t bind_point,
                MTL::Buffer* buffer, size_t offset, uint32_t stages) {
  if constexpr (std::is_same_v<EncoderAPI, Metal4EncoderAPI>) {
    state.arg_table->setAddress(buffer->gpuAddress() + offset, bind_point);
  } else {
    if constexpr (std::is_same_v<EncoderT, MTL::RenderCommandEncoder*>) {
      if (stages & EncoderSetBufferStage::Vertex) {
        encoder->setVertexBuffer(buffer, offset, bind_point);
      }
      if (stages & EncoderSetBufferStage::Mesh) {
        encoder->setMeshBuffer(buffer, offset, bind_point);
      }
      if (stages & EncoderSetBufferStage::Object) {
        encoder->setObjectBuffer(buffer, offset, bind_point);
      }
      if (stages & EncoderSetBufferStage::Fragment) {
        encoder->setFragmentBuffer(buffer, offset, bind_point);
      }
    } else {
      if (stages & EncoderSetBufferStage::Compute) {
        encoder->setBuffer(buffer, offset, bind_point);
      }
    }
  }
}

}  // namespace

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::begin_rendering(
    std::initializer_list<rhi::RenderingAttachmentInfo> attachments) {
  using RPAPI = typename EncoderAPI::RPAPI;
  using RPDesc = typename RPAPI::RPDesc;
  using ColorDesc = typename RPAPI::ColorDesc;
  using DepthDesc = typename RPAPI::DepthDesc;

  RPDesc* desc = RPAPI::alloc_desc();
  size_t color_att_i = 0;

  DepthDesc* depth_desc = nullptr;

  for (const auto& att : attachments) {
    if (att.type == rhi::RenderingAttachmentInfo::Type::DepthStencil) {
      depth_desc = RPAPI::alloc_depth_desc();
      depth_desc->setTexture(
          reinterpret_cast<MetalTexture*>(device_->get_tex(att.image))->texture());
      depth_desc->setLoadAction(mtl::util::convert(att.load_op));
      depth_desc->setStoreAction(mtl::util::convert(att.store_op));
      depth_desc->setClearDepth(att.clear_value.depth_stencil.depth);
      RPAPI::set_depth(desc, depth_desc);
    } else {
      ColorDesc* color_desc = RPAPI::get_color_desc(desc, color_att_i);
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

  EncoderAPI::start_render_encoder(encoder_state_, desc);
  ASSERT(encoder_state_.render_enc);
  if (!curr_debug_name_.empty()) {
    encoder_state_.render_enc->setLabel(mtl::util::string(curr_debug_name_));
  }
  flush_barriers();
  if (depth_desc) {
    depth_desc->release();
  }
  desc->release();

  set_buffer<EncoderAPI, typename EncoderAPI::RenderEnc>(
      encoder_state_, encoder_state_.render_enc, kIRDescriptorHeapBindPoint,
      device_->get_mtl_buf(device_->resource_descriptor_table_), 0,
      EncoderSetBufferStage::Vertex | EncoderSetBufferStage::Fragment |
          EncoderSetBufferStage::Object | EncoderSetBufferStage::Mesh);
  set_buffer<EncoderAPI, typename EncoderAPI::RenderEnc>(
      encoder_state_, encoder_state_.render_enc, kIRSamplerHeapBindPoint,
      device_->get_mtl_buf(device_->sampler_descriptor_table_), 0,
      EncoderSetBufferStage::Vertex | EncoderSetBufferStage::Fragment |
          EncoderSetBufferStage::Object | EncoderSetBufferStage::Mesh);
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::reset(MetalDevice* device,
                                            EncoderAPI::CommandBuffer cmd_buf) {
  device_ = device;
  ASSERT(cmd_buf);
  // TODO: refactor
  cmd_icb_mgr_.init(device_);
  encoder_state_.compute_enc = nullptr;
  encoder_state_.render_enc = nullptr;
  encoder_state_.blit_enc = nullptr;
  encoder_state_.cmd_buf = cmd_buf;
  root_layout_ = {};
  binding_table_ = {};

  if constexpr (std::is_same_v<EncoderAPI, Metal4EncoderAPI>) {
    if (!encoder_state_.arg_table) {
      MTL4::ArgumentTableDescriptor* desc = MTL4::ArgumentTableDescriptor::alloc()->init();
      desc->setInitializeBindings(false);
      desc->setMaxBufferBindCount(10);
      desc->setMaxSamplerStateBindCount(0);
      desc->setMaxTextureBindCount(0);
      NS::Error* err{};
      encoder_state_.arg_table = device_->get_device()->newArgumentTable(desc, &err);
      desc->release();
    }
  }
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::flush_barriers() {
  if (encoder_state_.compute_enc && device_->compute_enc_flush_stages_) {
    barrier_after_queue_stages<EncoderAPI>(encoder_state_.compute_enc,
                                           device_->compute_enc_flush_stages_,
                                           device_->compute_enc_dst_stages_);
    device_->compute_enc_flush_stages_ = 0;
    device_->compute_enc_dst_stages_ = 0;
  }

  if (encoder_state_.render_enc && device_->render_enc_flush_stages_) {
    barrier_after_queue_stages<EncoderAPI>(encoder_state_.render_enc,
                                           device_->render_enc_flush_stages_,
                                           device_->render_enc_dst_stages_);
    device_->render_enc_flush_stages_ = 0;
    device_->render_enc_dst_stages_ = 0;
  }
  if constexpr (std::is_same_v<EncoderAPI, Metal3EncoderAPI>) {
    if (encoder_state_.blit_enc && device_->blit_enc_flush_stages_) {
      barrier_after_queue_stages<EncoderAPI>(
          encoder_state_.blit_enc, device_->blit_enc_flush_stages_, device_->blit_enc_dst_stages_);
      device_->blit_enc_flush_stages_ = 0;
      device_->blit_enc_dst_stages_ = 0;
    }
  }
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::end_encoding() {
  flush_barriers();
  EncoderAPI::end_compute_encoder(encoder_state_.compute_enc);
  EncoderAPI::end_render_encoder(encoder_state_.render_enc);
  EncoderAPI::end_blit_encoder(encoder_state_.blit_enc);
  ASSERT(!done_);
  done_ = true;
  if constexpr (std::is_same_v<EncoderAPI, Metal4EncoderAPI>) {
    encoder_state_.cmd_buf->endCommandBuffer();
  }
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::start_compute_encoder() {
  if constexpr (std::is_same_v<EncoderAPI, Metal4EncoderAPI>) {
    auto& state = encoder_state_;
    EncoderAPI::end_render_encoder(state.render_enc);
    if (!state.compute_enc) {
      state.compute_enc = state.cmd_buf->computeCommandEncoder();
      state.compute_enc->setArgumentTable(state.arg_table);
    }
  } else {
    auto& state = encoder_state_;
    EncoderAPI::end_render_encoder(state.render_enc);
    EncoderAPI::end_blit_encoder(state.blit_enc);
    if (!state.compute_enc) {
      MTL::ComputePassDescriptor* desc = MTL::ComputePassDescriptor::alloc()->init();
      desc->setDispatchType(MTL::DispatchTypeConcurrent);
      MTL::ComputePassSampleBufferAttachmentDescriptor* desc_samp_buf_att =
          MTL::ComputePassSampleBufferAttachmentDescriptor::alloc()->init();
      // desc_samp_buf_att->setSampleBuffer(device_->get_curr_counter_buf());
      // desc_samp_buf_att->setStartOfEncoderSampleIndex(device_->curr_counter_buf_idx_++);
      // desc_samp_buf_att->setEndOfEncoderSampleIndex(device_->curr_counter_buf_idx_++);
      desc->sampleBufferAttachments()->setObject(desc_samp_buf_att, 0);
      state.compute_enc = state.cmd_buf->computeCommandEncoder(desc);
      desc->release();
    }
  }

  if (!curr_debug_name_.empty()) {
    encoder_state_.compute_enc->setLabel(mtl::util::string(curr_debug_name_));
  }
  flush_barriers();
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::start_blit_encoder() {
  if constexpr (std::is_same_v<EncoderAPI, Metal3EncoderAPI>) {
    EncoderAPI::start_blit_encoder(encoder_state_);
    if (!curr_debug_name_.empty()) {
      encoder_state_.blit_enc->setLabel(mtl::util::string(curr_debug_name_));
    }
    flush_barriers();
  } else {
    ALWAYS_ASSERT(0);
  }
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::bind_pipeline(rhi::PipelineHandle handle) {
  auto* pipeline = reinterpret_cast<MetalPipeline*>(device_->get_pipeline(handle));
  ASSERT(pipeline);
  if (pipeline->render_pso) {
    ASSERT(encoder_state_.render_enc);
    encoder_state_.render_enc->setRenderPipelineState(pipeline->render_pso);
  } else if (pipeline->compute_pso) {
    start_compute_encoder();
    encoder_state_.compute_enc->setComputePipelineState(pipeline->compute_pso);
  } else {
    LERROR("invalid pipeline for MetalCmdEncoder::bind_pipeline, exiting");
    exit(1);
  }
}

MetalRenderPassAPI3::RPDesc* MetalRenderPassAPI3::alloc_desc() {
  return MTL::RenderPassDescriptor::alloc()->init();
}

MetalRenderPassAPI3::ColorDesc* MetalRenderPassAPI3::get_color_desc(RPDesc* desc, size_t index) {
  return desc->colorAttachments()->object(index);
}

MetalRenderPassAPI3::DepthDesc* MetalRenderPassAPI3::alloc_depth_desc() {
  return MTL::RenderPassDepthAttachmentDescriptor::alloc()->init();
}

void MetalRenderPassAPI3::set_depth(RPDesc* desc, DepthDesc* depth_desc) {
  desc->setDepthAttachment(depth_desc);
}

MetalRenderPassAPI4::ColorDesc* MetalRenderPassAPI4::get_color_desc(RPDesc* desc, size_t index) {
  return desc->colorAttachments()->object(index);
}

MetalRenderPassAPI4::DepthDesc* MetalRenderPassAPI4::alloc_depth_desc() {
  return MTL::RenderPassDepthAttachmentDescriptor::alloc()->init();
}

void MetalRenderPassAPI4::set_depth(RPDesc* desc, DepthDesc* depth_desc) {
  desc->setDepthAttachment(depth_desc);
}

MetalRenderPassAPI4::RPDesc* MetalRenderPassAPI4::alloc_desc() {
  return MTL4::RenderPassDescriptor::alloc()->init();
}

void Metal3EncoderAPI::end_compute_encoder(ComputeEnc& enc) {
  if (enc) {
    enc->endEncoding();
    enc = nullptr;
  }
}
void Metal3EncoderAPI::end_blit_encoder(BlitEnc& enc) {
  if (enc) {
    enc->endEncoding();
    enc = nullptr;
  }
}
void Metal3EncoderAPI::end_render_encoder(RenderEnc& enc) {
  if (enc) {
    enc->endEncoding();
    enc = nullptr;
  }
}

void Metal3EncoderAPI::start_compute_encoder(EncoderState& state) {
  ASSERT(0);
  end_blit_encoder(state.blit_enc);
  end_render_encoder(state.render_enc);
  if (!state.compute_enc) {
    // MTL::ComputePassDescriptor* desc = MTL::ComputePassDescriptor::alloc()->init();
    // desc->setDispatchType(MTL::DispatchTypeConcurrent);
    // MTL::ComputePassSampleBufferAttachmentDescriptor* desc_samp_buf_att =
    //     MTL::ComputePassSampleBufferAttachmentDescriptor::alloc()->init();
    // desc_samp_buf_att->setSampleBuffer(state.cmd_buf->sampleBuffer());
    // desc_samp_buf_att->setStartOfEncoderSampleIndex(0);
    // desc_samp_buf_att->setEndOfEncoderSampleIndex(1);
    // desc->sampleBufferAttachments()->setObject(desc_samp_buf_att, 0);
    // desc->release();
    // state.compute_enc = state.cmd_buf->computeCommandEncoder(desc);
    state.compute_enc = state.cmd_buf->computeCommandEncoder(MTL::DispatchTypeConcurrent);
    // if constexpr (std::is_same_v<EncoderAPI, Metal3EncoderAPI>) {
    //   encoder_state_.blit_enc->sampleCountersInBuffer(
    //       device_->test_counter_buf_[device_->frame_idx()], device_->curr_counter_buf_idx_++,
    //       false);
    //   LINFO("blit start");
    // }
  }
}

void Metal3EncoderAPI::start_render_encoder(EncoderState& state, RPAPI::RPDesc* desc) {
  end_compute_encoder(state.compute_enc);
  end_blit_encoder(state.blit_enc);
  if (!state.render_enc) {
    state.render_enc = state.cmd_buf->renderCommandEncoder(desc);
    ASSERT(state.render_enc);
  }
}

void Metal3EncoderAPI::start_blit_encoder(EncoderState& state) {
  if (!state.blit_enc) {
    state.blit_enc = state.cmd_buf->blitCommandEncoder();
  }
}

void Metal3EncoderAPI::end_all_encoders(RenderEnc& r, ComputeEnc& c, BlitEnc& b) {
  if (r) {
    r->endEncoding();
    r = nullptr;
  }
  if (c) {
    c->endEncoding();
    c = nullptr;
  }
  if (b) {
    b->endEncoding();
    b = nullptr;
  }
}

void Metal4EncoderAPI::end_compute_encoder(ComputeEnc& enc) {
  if (enc) {
    enc->endEncoding();
    enc = nullptr;
  }
}

void Metal4EncoderAPI::end_render_encoder(RenderEnc& enc) {
  if (enc) {
    enc->endEncoding();
    enc = nullptr;
  }
}

void Metal4EncoderAPI::start_compute_encoder(EncoderState& state) {
  end_render_encoder(state.render_enc);
  if (!state.compute_enc) {
    state.compute_enc = state.cmd_buf->computeCommandEncoder();
    state.compute_enc->setArgumentTable(state.arg_table);
  }
}

void Metal4EncoderAPI::start_render_encoder(EncoderState& state, RPAPI::RPDesc* desc) {
  end_compute_encoder(state.compute_enc);
  if (!state.render_enc) {
    ASSERT(state.cmd_buf);
    state.render_enc = state.cmd_buf->renderCommandEncoder(desc);
    state.render_enc->setArgumentTable(state.arg_table,
                                       MTL::RenderStageVertex | MTL::RenderStageFragment |
                                           MTL::RenderStageObject | MTL::RenderStageMesh);
  }
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::set_viewport(glm::uvec2 min, glm::uvec2 extent) {
  MTL::Viewport vp;
  vp.originX = min.x;
  vp.originY = min.y;
  vp.width = extent.x;
  vp.height = extent.y;
  vp.znear = 0.0f;
  vp.zfar = 1.f;
  encoder_state_.render_enc->setViewport(vp);
}
template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::set_scissor(glm::uvec2 min, glm::uvec2 extent) {
  MTL::ScissorRect r{.x = min.x, .y = min.y, .width = extent.x, .height = extent.y};
  encoder_state_.render_enc->setScissorRect(r);
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::draw_primitives(rhi::PrimitiveTopology topology,
                                                      size_t vertex_start, size_t count,
                                                      size_t instance_count) {
  flush_binds();
  encoder_state_.render_enc->drawPrimitives(mtl::util::convert(topology), vertex_start, count,
                                            instance_count);
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::push_constants(void* data, size_t size) {
  // TODO: hacky lmao
  // TODO: move this to the start of compute encoder
  if constexpr (std::is_same_v<EncoderAPI, Metal4EncoderAPI>) {
    set_buffer<EncoderAPI, typename EncoderAPI::RenderEnc>(
        encoder_state_, nullptr, kIRDescriptorHeapBindPoint,
        device_->get_mtl_buf(device_->resource_descriptor_table_), 0, 0);
    set_buffer<EncoderAPI, typename EncoderAPI::RenderEnc>(
        encoder_state_, nullptr, kIRSamplerHeapBindPoint,
        device_->get_mtl_buf(device_->sampler_descriptor_table_), 0, 0);
  }
  ASSERT(size <= sizeof(root_layout_.constants) - 8);
  memcpy(root_layout_.constants, data, size);
  push_constant_dirty_ = true;
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::draw_indexed_primitives(
    rhi::PrimitiveTopology topology, rhi::BufferHandle index_buf, size_t index_start, size_t count,
    size_t instance_count, size_t base_vertex_idx, size_t base_instance,
    rhi::IndexType index_type) {
  root_layout_.constants[20] = base_instance;
  root_layout_.constants[21] = base_vertex_idx;
  flush_binds();

  auto* buf = device_->get_mtl_buf(index_buf);
  if constexpr (std::is_same_v<EncoderAPI, Metal4EncoderAPI>) {
    encoder_state_.render_enc->drawIndexedPrimitives(
        mtl::util::convert(topology), count,
        index_type == rhi::IndexType::Uint32 ? MTL::IndexTypeUInt32 : MTL::IndexTypeUInt16,
        buf->gpuAddress() + index_start, buf->length(), instance_count, 0, 0);
  } else {
    encoder_state_.render_enc->drawIndexedPrimitives(
        mtl::util::convert(topology), count,
        index_type == rhi::IndexType::Uint32 ? MTL::IndexTypeUInt32 : MTL::IndexTypeUInt16, buf,
        index_start, instance_count, 0, 0);
  }
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::set_depth_stencil_state(rhi::CompareOp depth_compare_op,
                                                              bool depth_write_enabled) {
  // TODO: cache this pls
  MTL::DepthStencilDescriptor* depth_stencil_desc = MTL::DepthStencilDescriptor::alloc()->init();
  depth_stencil_desc->setDepthCompareFunction(mtl::util::convert(depth_compare_op));
  depth_stencil_desc->setDepthWriteEnabled(depth_write_enabled);
  encoder_state_.render_enc->setDepthStencilState(
      device_->get_device()->newDepthStencilState(depth_stencil_desc));
  depth_stencil_desc->release();
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::set_wind_order(rhi::WindOrder wind_order) {
  encoder_state_.render_enc->setFrontFacingWinding(mtl::util::convert(wind_order));
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::set_cull_mode(rhi::CullMode cull_mode) {
  encoder_state_.render_enc->setCullMode(mtl::util::convert(cull_mode));
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::upload_texture_data(rhi::BufferHandle src_buf,
                                                          size_t src_offset,
                                                          size_t src_bytes_per_row,
                                                          rhi::TextureHandle dst_tex) {
  upload_texture_data(src_buf, src_offset, src_bytes_per_row, dst_tex,
                      device_->get_tex(dst_tex)->desc().dims, glm::uvec3{0, 0, 0}, -1);
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::upload_texture_data(
    rhi::BufferHandle src_buf, size_t src_offset, size_t src_bytes_per_row,
    rhi::TextureHandle dst_tex, glm::uvec3 src_size, glm::uvec3 dst_origin, int mip_level) {
  start_blit_equivalent_encoder();
  auto* buf = device_->get_mtl_buf(src_buf);
  auto* tex = device_->get_mtl_tex(dst_tex);
  ALWAYS_ASSERT(buf);
  ALWAYS_ASSERT(tex);
  MTL::Size img_size = MTL::Size::Make(src_size.x, src_size.y, src_size.z);
  auto mip = mip_level < 0 ? 0 : mip_level;
  if constexpr (std::is_same_v<EncoderAPI, Metal4EncoderAPI>) {
    encoder_state_.compute_enc->copyFromBuffer(
        buf, src_offset, src_bytes_per_row, 0, img_size, tex, 0, mip,
        MTL::Origin::Make(dst_origin.x, dst_origin.y, dst_origin.z));
    encoder_state_.compute_enc->barrierAfterEncoderStages(MTL::StageBlit, MTL::StageBlit,
                                                          MTL4::VisibilityOptionDevice);
  } else {
    encoder_state_.blit_enc->copyFromBuffer(
        buf, src_offset, src_bytes_per_row, 0, img_size, tex, 0, mip,
        MTL::Origin::Make(dst_origin.x, dst_origin.y, dst_origin.z));
  }

  if constexpr (std::is_same_v<EncoderAPI, Metal4EncoderAPI>) {
    if (mip_level < 0 && tex->mipmapLevelCount() > 1) {
      encoder_state_.compute_enc->generateMipmaps(tex);
    }
  } else {
    if (mip_level < 0 && tex->mipmapLevelCount() > 1) {
      encoder_state_.blit_enc->generateMipmaps(tex);
    }
  }
  if constexpr (std::is_same_v<EncoderAPI, Metal4EncoderAPI>) {
    encoder_state_.compute_enc->barrierAfterEncoderStages(MTL::StageBlit, MTL::StageBlit,
                                                          MTL4::VisibilityOptionDevice);
  }
}

template <typename EncoderAPI>
uint32_t MetalCmdEncoderBase<EncoderAPI>::prepare_indexed_indirect_draws(
    rhi::BufferHandle indirect_buf, size_t offset, size_t tot_draw_cnt, rhi::BufferHandle index_buf,
    size_t index_buf_offset, void* push_constant_data, size_t push_constant_size,
    size_t vertex_stride) {
  ASSERT(push_constant_size <= sizeof(RootLayout) - 8);
  auto [pc_buf, pc_buf_offset] = device_->push_constant_allocator_->alloc(sizeof(RootLayout));
  auto* root_layout_ptr = (RootLayout*)((uint8_t*)pc_buf->contents() + pc_buf_offset);
  memcpy(root_layout_ptr->constants, push_constant_data, push_constant_size);

  start_compute_encoder();

  ASSERT(index_buf.is_valid());
  auto* index_buffer = device_->get_mtl_buf(index_buf);
  ASSERT(index_buffer);
  set_buffer<EncoderAPI, typename EncoderAPI::ComputeEnc>(
      encoder_state_, encoder_state_.compute_enc, 5, index_buffer, index_buf_offset,
      EncoderSetBufferStage::Compute);

  set_buffer<EncoderAPI, typename EncoderAPI::ComputeEnc>(
      encoder_state_, encoder_state_.compute_enc, 6,
      device_->get_mtl_buf(device_->resource_descriptor_table_), 0, EncoderSetBufferStage::Compute);

  set_buffer<EncoderAPI, typename EncoderAPI::ComputeEnc>(
      encoder_state_, encoder_state_.compute_enc, 7,
      device_->get_mtl_buf(device_->sampler_descriptor_table_), 0, EncoderSetBufferStage::Compute);

  ASSERT(device_->get_psos().dispatch_indirect_pso);
  ASSERT(device_->get_buf(indirect_buf)->desc().usage & rhi::BufferUsage_Indirect);

  auto [indirect_buf_id, icbs] = device_->icb_mgr_draw_indexed_.alloc(indirect_buf, tot_draw_cnt);

  encoder_state_.compute_enc->setComputePipelineState(device_->get_psos().dispatch_indirect_pso);

  for (int i = 0, rem_draw_count = tot_draw_cnt; i < (int)icbs.size();
       i++, rem_draw_count -= k_max_draws_per_icb) {
    uint32_t draw_cnt = std::min<uint32_t>(k_max_draws_per_icb, rem_draw_count);
    auto* icb = icbs[i];
    ASSERT((icb && icb->size() == draw_cnt));

    cmd_icb_mgr_.init_icb_arg_encoder_and_buf_and_set_icb(icbs, i);

    set_buffer<EncoderAPI, typename EncoderAPI::ComputeEnc>(
        encoder_state_, encoder_state_.compute_enc, 0, pc_buf, pc_buf_offset,
        EncoderSetBufferStage::Compute);

    struct Args2 {
      uint32_t draw_cnt;
      uint32_t stride;
    };

    auto [args2_buf, args2_offset] = device_->test_allocator_->alloc(sizeof(Args2));
    auto* args2 = (Args2*)((uint8_t*)args2_buf->contents() + args2_offset);
    args2->draw_cnt = draw_cnt;
    args2->stride = vertex_stride;

    set_buffer<EncoderAPI, typename EncoderAPI::ComputeEnc>(
        encoder_state_, encoder_state_.compute_enc, 1, args2_buf, args2_offset,
        EncoderSetBufferStage::Compute);

    set_buffer<EncoderAPI, typename EncoderAPI::ComputeEnc>(
        encoder_state_, encoder_state_.compute_enc, 2, cmd_icb_mgr_.get_icb(i), 0,
        EncoderSetBufferStage::Compute);

    auto [out_pc_arg_buf, out_pc_arg_buf_offset] =
        device_->test_allocator_->alloc(draw_cnt * sizeof(RootLayout));

    set_buffer<EncoderAPI, typename EncoderAPI::ComputeEnc>(
        encoder_state_, encoder_state_.compute_enc, 3, out_pc_arg_buf, out_pc_arg_buf_offset,
        EncoderSetBufferStage::Compute);

    auto* indirect_buffer = device_->get_mtl_buf(indirect_buf);
    ASSERT(indirect_buffer);

    size_t iter_offset = i * k_max_draws_per_icb * sizeof(IndexedIndirectDrawCmd);

    set_buffer<EncoderAPI, typename EncoderAPI::ComputeEnc>(
        encoder_state_, encoder_state_.compute_enc, 4, indirect_buffer, offset + iter_offset,
        EncoderSetBufferStage::Compute);

    uint32_t threads_per_tg_x = 64;
    uint32_t tg_x = (draw_cnt + threads_per_tg_x - 1) / threads_per_tg_x;
    encoder_state_.compute_enc->dispatchThreadgroups(MTL::Size::Make(tg_x, 1, 1),
                                                     MTL::Size::Make(threads_per_tg_x, 1, 1));
  }

  barrier_after_queue_stages<EncoderAPI>(encoder_state_.compute_enc, MTL::StageDispatch,
                                         MTL::StageAll);

  return indirect_buf_id;
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::barrier(rhi::PipelineStage src_stage, rhi::AccessFlags,
                                              rhi::PipelineStage dst_stage, rhi::AccessFlags) {
  auto src_mtl_stage = mtl::util::convert_stage(src_stage);
  auto dst_mtl_stage = mtl::util::convert_stage(dst_stage);
  if (dst_mtl_stage & (MTL::StageDispatch | MTL::StageBlit)) {
    device_->compute_enc_flush_stages_ |= src_mtl_stage;
    device_->compute_enc_dst_stages_ |= dst_mtl_stage;
  }
  if (dst_mtl_stage & (MTL::StageVertex | MTL::StageFragment | MTL::StageObject | MTL::StageMesh)) {
    device_->render_enc_flush_stages_ |= src_mtl_stage;
    device_->render_enc_dst_stages_ |= dst_mtl_stage;
  }
  if constexpr (std::is_same_v<EncoderAPI, Metal3EncoderAPI>) {
    if (dst_mtl_stage & MTL::StageBlit) {
      device_->blit_enc_flush_stages_ |= src_mtl_stage;
      device_->blit_enc_dst_stages_ |= dst_mtl_stage;
    }
  }
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::barrier(rhi::BufferHandle, rhi::PipelineStage src_stage,
                                              rhi::AccessFlags, rhi::PipelineStage dst_stage,
                                              rhi::AccessFlags) {
  auto src_mtl_stage = mtl::util::convert_stage(src_stage);
  auto dst_mtl_stage = mtl::util::convert_stage(dst_stage);
  if (dst_mtl_stage & (MTL::StageDispatch | MTL::StageBlit)) {
    device_->compute_enc_flush_stages_ |= src_mtl_stage;
    device_->compute_enc_dst_stages_ |= dst_mtl_stage;
  }
  if (dst_mtl_stage & (MTL::StageVertex | MTL::StageFragment | MTL::StageObject | MTL::StageMesh)) {
    device_->render_enc_flush_stages_ |= src_mtl_stage;
    device_->render_enc_dst_stages_ |= dst_mtl_stage;
  }
  if constexpr (std::is_same_v<EncoderAPI, Metal3EncoderAPI>) {
    if (dst_mtl_stage & MTL::StageBlit) {
      device_->blit_enc_flush_stages_ |= src_mtl_stage;
      device_->blit_enc_dst_stages_ |= dst_mtl_stage;
    }
  }
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::draw_indexed_indirect(rhi::BufferHandle indirect_buf,
                                                            uint32_t indirect_buf_id,
                                                            size_t draw_cnt, size_t offset_i) {
  ASSERT(encoder_state_.render_enc);
  ASSERT(indirect_buf.is_valid());
  const auto& icbs = device_->icb_mgr_draw_indexed_.get(indirect_buf, indirect_buf_id);
  size_t rem_draws = draw_cnt;
  for (size_t i = 0, off = 0; i < icbs.size() && off < draw_cnt; i++, off += k_max_draws_per_icb) {
    encoder_state_.render_enc->executeCommandsInBuffer(
        icbs[i], NS::Range::Make(offset_i, std::min<uint32_t>(k_max_draws_per_icb, rem_draws)));
    rem_draws -= k_max_draws_per_icb;
  }
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::copy_tex_to_buf(rhi::TextureHandle src_tex, size_t src_slice,
                                                      size_t src_level, rhi::BufferHandle dst_buf,
                                                      size_t dst_offset) {
  start_compute_encoder();
  auto* tex = device_->get_mtl_tex(src_tex);
  auto* buf = device_->get_mtl_buf(dst_buf);
  size_t bytes_per_row = tex->width() * 4;
  if constexpr (std::is_same_v<EncoderAPI, Metal4EncoderAPI>) {
    encoder_state_.compute_enc->copyFromTexture(
        tex, src_slice, src_level, MTL::Origin::Make(0, 0, 0),
        MTL::Size::Make(tex->width(), tex->height(), tex->depth()), buf, dst_offset, bytes_per_row,
        0);
  } else {
    encoder_state_.blit_enc->copyFromTexture(
        tex, src_slice, src_level, MTL::Origin::Make(0, 0, 0),
        MTL::Size::Make(tex->width(), tex->height(), tex->depth()), buf, dst_offset, bytes_per_row,
        0);
  }
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::draw_mesh_threadgroups(
    glm::uvec3 thread_groups, glm::uvec3 threads_per_task_thread_group,
    glm::uvec3 threads_per_mesh_thread_group) {
  flush_binds();
  encoder_state_.render_enc->drawMeshThreadgroups(
      MTL::Size::Make(thread_groups.x, thread_groups.y, thread_groups.z),
      MTL::Size::Make(threads_per_task_thread_group.x, threads_per_task_thread_group.y,
                      threads_per_task_thread_group.z),
      MTL::Size::Make(threads_per_mesh_thread_group.x, threads_per_mesh_thread_group.y,
                      threads_per_mesh_thread_group.z));
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::pop_debug_group() {
  ALWAYS_ASSERT(push_debug_group_stack_size_ > 0);
  push_debug_group_stack_size_--;
  encoder_state_.cmd_buf->popDebugGroup();
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::push_debug_group(const char* name) {
  encoder_state_.cmd_buf->pushDebugGroup(mtl::util::string(name));
  push_debug_group_stack_size_++;
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::dispatch_compute(glm::uvec3 thread_groups,
                                                       glm::uvec3 threads_per_threadgroup) {
  flush_binds();
  // TODO: do this on start of compute encoder and cache any changes during internal stuff
  set_buffer<EncoderAPI>(encoder_state_, encoder_state_.compute_enc, kIRDescriptorHeapBindPoint,
                         device_->get_mtl_buf(device_->resource_descriptor_table_), 0,
                         EncoderSetBufferStage::Compute);
  set_buffer<EncoderAPI>(encoder_state_, encoder_state_.compute_enc, kIRSamplerHeapBindPoint,
                         device_->get_mtl_buf(device_->sampler_descriptor_table_), 0,
                         EncoderSetBufferStage::Compute);
  encoder_state_.compute_enc->dispatchThreadgroups(
      MTL::Size::Make(thread_groups.x, thread_groups.y, thread_groups.z),
      MTL::Size::Make(threads_per_threadgroup.x, threads_per_threadgroup.y,
                      threads_per_threadgroup.z));
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::fill_buffer(rhi::BufferHandle handle, uint32_t offset_bytes,
                                                  uint32_t size, uint32_t value) {
  auto* buf = device_->get_mtl_buf(handle);
  ASSERT(buf);
  start_blit_equivalent_encoder();
  if constexpr (std::is_same_v<EncoderAPI, Metal4EncoderAPI>) {
    encoder_state_.compute_enc->fillBuffer(buf, NS::Range::Make(offset_bytes, size), value);
  } else {
    encoder_state_.blit_enc->fillBuffer(buf, NS::Range::Make(offset_bytes, size), value);
  }
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::draw_mesh_threadgroups_indirect(
    rhi::BufferHandle indirect_buf, size_t indirect_buf_offset,
    glm::uvec3 threads_per_task_thread_group, glm::uvec3 threads_per_mesh_thread_group) {
  flush_binds();
  auto* buf = device_->get_mtl_buf(indirect_buf);
  ASSERT(buf);
  if constexpr (std::is_same_v<EncoderAPI, Metal4EncoderAPI>) {
    encoder_state_.render_enc->drawMeshThreadgroups(
        buf->gpuAddress() + indirect_buf_offset,
        MTL::Size::Make(threads_per_task_thread_group.x, threads_per_task_thread_group.y,
                        threads_per_task_thread_group.z),
        MTL::Size::Make(threads_per_mesh_thread_group.x, threads_per_mesh_thread_group.y,
                        threads_per_mesh_thread_group.z));
  } else {
    encoder_state_.render_enc->drawMeshThreadgroups(
        buf, indirect_buf_offset,
        MTL::Size::Make(threads_per_task_thread_group.x, threads_per_task_thread_group.y,
                        threads_per_task_thread_group.z),
        MTL::Size::Make(threads_per_mesh_thread_group.x, threads_per_mesh_thread_group.y,
                        threads_per_mesh_thread_group.z));
  }
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::copy_buffer_to_buffer(rhi::BufferHandle src_buf,
                                                            size_t src_offset,
                                                            rhi::BufferHandle dst_buf,
                                                            size_t dst_offset, size_t size) {
  start_blit_equivalent_encoder();
  auto* src_b = (MetalBuffer*)device_->get_buf(src_buf);
  auto* dst_b = (MetalBuffer*)device_->get_buf(dst_buf);
  if (src_b->desc().storage_mode != rhi::StorageMode::GPUOnly &&
      dst_b->desc().storage_mode != rhi::StorageMode::GPUOnly) {
    // both buffers are CPU accessible, do a memcpy
    memcpy((uint8_t*)dst_b->buffer()->contents() + dst_offset,
           (uint8_t*)src_b->buffer()->contents() + src_offset, size);
  } else {
    if constexpr (std::is_same_v<EncoderAPI, Metal4EncoderAPI>) {
      encoder_state_.compute_enc->copyFromBuffer(src_b->buffer(), src_offset, dst_b->buffer(),
                                                 dst_offset, size);
    } else {
      encoder_state_.blit_enc->copyFromBuffer(src_b->buffer(), src_offset, dst_b->buffer(),
                                              dst_offset, size);
    }
  }
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::start_blit_equivalent_encoder() {
  if constexpr (std::is_same_v<EncoderAPI, Metal4EncoderAPI>) {
    start_compute_encoder();
  } else {
    start_blit_encoder();
  }
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::set_debug_name(const char* name) {
  curr_debug_name_ = name;
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::end_rendering() {
  EncoderAPI::end_render_encoder(encoder_state_.render_enc);
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::flush_binds() {
  if (binding_table_dirty_) {
    ResourceTable table = {};
    for (uint32_t i = 0; i < ARRAY_SIZE(binding_table_.SRV); i++) {
      if (!generational_handle_u64_is_valid(binding_table_.SRV[i])) {
        continue;
      }
      // if (!binding_table_.SRV[i].is_valid()) {
      //   continue;
      // }
      IRDescriptorTableEntry entry;
      const auto resource_id = binding_table_.SRV_subresources[i];
      if (resource_id == DescriptorBindingTable::k_buffer_resource) {
        const auto* buf = device_->get_mtl_buf(rhi::BufferHandle{binding_table_.SRV[i]});
        const size_t metadata = buf->length();
        ASSERT(i - ROOT_CBV_COUNT < ARRAY_SIZE(table.cbvs));
        entry.gpuVA = buf->gpuAddress() + binding_table_.SRV_offsets[i];
        entry.textureViewID = 0;
        entry.metadata = metadata;
      } else if (resource_id == DescriptorBindingTable::k_tex_resource) {
        const auto* tex = device_->get_mtl_tex(rhi::TextureHandle{binding_table_.SRV[i]});
        entry.gpuVA = 0;
        entry.textureViewID = tex->gpuResourceID()._impl;
        entry.metadata = 0;
      } else {
        const auto* tex_view = device_->get_tex_view(rhi::TextureHandle{binding_table_.SRV[i]},
                                                     binding_table_.SRV_subresources[i]);
        entry.gpuVA = 0;
        entry.textureViewID = tex_view->tex->gpuResourceID()._impl;
        entry.metadata = 0;
      }
      table.srvs[i] = entry;
    }

    for (uint32_t i = 0; i < ARRAY_SIZE(binding_table_.UAV); i++) {
      if (!generational_handle_u64_is_valid(binding_table_.UAV[i])) {
        continue;
      }
      IRDescriptorTableEntry entry;
      auto id = binding_table_.UAV_subresources[i];
      if (id == DescriptorBindingTable::k_buffer_resource) {
        const auto* buf = device_->get_mtl_buf(rhi::BufferHandle{binding_table_.UAV[i]});
        const size_t metadata = buf->length();
        entry.gpuVA = buf->gpuAddress() + binding_table_.UAV_offsets[i];
        entry.textureViewID = 0;
        entry.metadata = metadata;
      } else if (id == DescriptorBindingTable::k_tex_resource) {
        auto* tex = device_->get_mtl_tex(rhi::TextureHandle{binding_table_.UAV[i]});
        entry.gpuVA = 0;
        entry.textureViewID = tex->gpuResourceID()._impl;
        entry.metadata = 0;
      } else {
        auto* tex_view = device_->get_tex_view(rhi::TextureHandle{binding_table_.UAV[i]},
                                               binding_table_.UAV_subresources[i]);
        entry.gpuVA = 0;
        entry.textureViewID = tex_view->tex->gpuResourceID()._impl;
        entry.metadata = 0;
      }
      table.uavs[i] = entry;
    }

    // TODO: dirty root flag instead of only dirty resources flag.
    for (uint32_t i = 0; i < ROOT_CBV_COUNT; i++) {
      if (!binding_table_.CBV[i].is_valid()) {
        continue;
      }
      auto* buf = device_->get_mtl_buf(binding_table_.CBV[i]);
      root_layout_.root_cbvs[i] = buf->gpuAddress() + binding_table_.CBV_offsets[i];
      ASSERT(root_layout_.root_cbvs[i] % 256 == 0);
    }

    for (uint32_t i = ARRAY_SIZE(root_layout_.root_cbvs); i < ARRAY_SIZE(binding_table_.CBV); i++) {
      if (!binding_table_.CBV[i].is_valid()) {
        continue;
      }
      const auto* buf = device_->get_mtl_buf(binding_table_.CBV[i]);
      const auto gpu_va = buf->gpuAddress() + binding_table_.CBV_offsets[i];
      ASSERT(gpu_va % 256 == 0);
      const size_t metadata = buf->length();
      ASSERT(i - ROOT_CBV_COUNT < ARRAY_SIZE(table.cbvs));
      IRDescriptorTableSetBuffer(table.cbvs + (i - ARRAY_SIZE(root_layout_.root_cbvs)), gpu_va,
                                 metadata);
    }

    binding_table_dirty_ = false;
    auto [binding_table, binding_table_offset] =
        device_->push_constant_allocator_->alloc(sizeof(ResourceTable));
    root_layout_.resource_table_ptr = binding_table->gpuAddress() + binding_table_offset;
    memcpy((uint8_t*)binding_table->contents() + binding_table_offset, &table,
           sizeof(ResourceTable));
  }

  auto [pc_buf, pc_buf_offset] = device_->push_constant_allocator_->alloc(sizeof(RootLayout));
  auto* root_layout = reinterpret_cast<RootLayout*>((uint8_t*)pc_buf->contents() + pc_buf_offset);
  memcpy(root_layout, &root_layout_, sizeof(RootLayout));

  if (encoder_state_.compute_enc) {
    set_buffer<EncoderAPI>(encoder_state_, encoder_state_.compute_enc, kIRArgumentBufferBindPoint,
                           pc_buf, pc_buf_offset, EncoderSetBufferStage::Compute);
  } else if (encoder_state_.render_enc) {
    set_buffer<EncoderAPI>(encoder_state_, encoder_state_.render_enc, kIRArgumentBufferBindPoint,
                           pc_buf, pc_buf_offset,
                           EncoderSetBufferStage::Vertex | EncoderSetBufferStage::Fragment |
                               EncoderSetBufferStage::Object | EncoderSetBufferStage::Mesh);
  } else {
    ASSERT(0 && "no encoder to flush binds to");
  }
  push_constant_dirty_ = false;
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::bind_srv(rhi::TextureHandle texture, uint32_t slot,
                                               int subresource_id) {
  ASSERT(slot < ARRAY_SIZE(binding_table_.SRV));
  binding_table_.SRV[slot] = texture.to64();
  binding_table_.SRV_subresources[slot] = subresource_id;
  binding_table_dirty_ = true;
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::bind_uav(rhi::TextureHandle texture, uint32_t slot,
                                               int subresource_id) {
  ASSERT(slot < ARRAY_SIZE(binding_table_.UAV));
  binding_table_.UAV[slot] = texture.to64();
  binding_table_.UAV_subresources[slot] = subresource_id;
  binding_table_dirty_ = true;
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::bind_cbv(rhi::BufferHandle buffer, uint32_t slot,
                                               size_t offset_bytes) {
  ASSERT(slot < ARRAY_SIZE(binding_table_.SRV));
  binding_table_.CBV[slot] = buffer;
  binding_table_.CBV_offsets[slot] = offset_bytes;
  binding_table_dirty_ = true;
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::bind_uav(rhi::BufferHandle buffer, uint32_t slot,
                                               size_t offset_bytes) {
  ASSERT(slot < ARRAY_SIZE(binding_table_.SRV));
  binding_table_.UAV[slot] = buffer.to64();
  binding_table_.UAV_subresources[slot] = DescriptorBindingTable::k_buffer_resource;
  binding_table_.UAV_offsets[slot] = offset_bytes;
  binding_table_dirty_ = true;
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::bind_srv(rhi::BufferHandle buffer, uint32_t slot,
                                               size_t offset_bytes) {
  ASSERT(slot < ARRAY_SIZE(binding_table_.SRV));
  binding_table_.SRV[slot] = buffer.to64();
  binding_table_.SRV_subresources[slot] = DescriptorBindingTable::k_buffer_resource;
  binding_table_.SRV_offsets[slot] = offset_bytes;
  binding_table_dirty_ = true;
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::write_timestamp(rhi::QueryPoolHandle query_pool,
                                                      uint32_t query_index) {
  // LINFO("writing timestamp {}", query_index);
  if constexpr (std::is_same_v<EncoderAPI, Metal4EncoderAPI>) {
    auto* pool = (MetalQueryPool*)device_->get_query_pool(query_pool);
    constexpr MTL4::TimestampGranularity granularity = MTL4::TimestampGranularityPrecise;
    if (encoder_state_.compute_enc) {
      ASSERT(query_index == 0);
      encoder_state_.compute_enc->writeTimestamp(granularity, pool->heap_, query_index);
      encoder_state_.compute_enc->barrierAfterStages(MTL::StageAll, MTL::StageAll,
                                                     MTL4::VisibilityOptionDevice);
    } else if (encoder_state_.render_enc) {
      ASSERT(query_index == 1);
      encoder_state_.render_enc->writeTimestamp(granularity, MTL::RenderStageVertex, pool->heap_,
                                                query_index);
      encoder_state_.render_enc->barrierAfterStages(MTL::StageAll, MTL::StageAll,
                                                    MTL4::VisibilityOptionDevice);
      if (!tmp_fences_[device_->frame_idx()]) {
        tmp_fences_[device_->frame_idx()] = device_->get_device()->newFence();
      }
      encoder_state_.render_enc->updateFence(tmp_fences_[device_->frame_idx()],
                                             MTL::StageVertex | MTL::StageFragment);
    } else {
      ASSERT(0 && "not using this right now");
      encoder_state_.cmd_buf->writeTimestampIntoHeap(pool->heap_, query_index);
    }
  } else {
    LWARN("query pools not supported in Metal3");
  }
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::set_label(const std::string& label) {
  if constexpr (std::is_same_v<EncoderAPI, Metal4EncoderAPI>) {
    encoder_state_.cmd_buf->setLabel(mtl::util::string(label.c_str()));
  }
}

template <typename EncoderAPI>
void MetalCmdEncoderBase<EncoderAPI>::query_resolve(rhi::QueryPoolHandle query_pool,
                                                    uint32_t start_query, uint32_t query_count,
                                                    rhi::BufferHandle dst_buffer,
                                                    size_t dst_offset) {
  if constexpr (std::is_same_v<EncoderAPI, Metal4EncoderAPI>) {
    auto* pool = (MetalQueryPool*)device_->get_query_pool(query_pool);
    auto* buf = device_->get_mtl_buf(dst_buffer);
    ASSERT(device_->get_device()->sizeOfCounterHeapEntry(MTL4::CounterHeapTypeTimestamp) ==
           sizeof(uint64_t));
    auto range =
        MTL4::BufferRange::Make(buf->gpuAddress() + dst_offset, query_count * sizeof(uint64_t));
    ASSERT(buf->length() >= dst_offset + query_count * sizeof(uint64_t));
    encoder_state_.cmd_buf->resolveCounterHeap(pool->heap_,
                                               NS::Range::Make(start_query, query_count), range,
                                               tmp_fences_[device_->frame_idx()], nullptr);
  } else {
    LWARN("query pools not supported in Metal3");
  }
}
template class MetalCmdEncoderBase<Metal3EncoderAPI>;
template class MetalCmdEncoderBase<Metal4EncoderAPI>;
