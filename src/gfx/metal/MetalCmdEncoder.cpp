#include "MetalCmdEncoder.hpp"

#include <sys/wait.h>

// clang-format off
#include <Metal/MTLComputeCommandEncoder.hpp>
#include <Metal/Metal.hpp>
#include "gfx/metal/Config.hpp"
#include "gfx/metal/RootLayout.hpp"
#define IR_RUNTIME_METALCPP
#include <metal_irconverter_runtime/metal_irconverter_runtime_wrapper.h>
// clang-format on

#include <Metal/MTLCommandEncoder.hpp>

#include "core/EAssert.hpp"
#include "gfx/metal/MetalDevice.hpp"
#include "gfx/metal/MetalUtil.hpp"
#include "gfx/rhi/GFXTypes.hpp"

enum EncoderType {
  EncoderType_Render = 1,
  EncoderType_Compute = 1 << 1,
  EncoderType_Blit = 1 << 2,
};

namespace {

enum EncoderSetBufferStage : uint32_t {
  Vertex = (1 << 0),
  Fragment = (1 << 1),
  Mesh = (1 << 2),
  Object = (1 << 3),
  Compute = (1 << 4)
};

}  // namespace

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::set_buffer(uint32_t bind_point, MTL::Buffer* buffer,
                                              size_t offset, uint32_t stages) {
  if constexpr (UseMTL4) {
    m4_state().arg_table->setAddress(buffer->gpuAddress() + offset, bind_point);
  } else {
    if (stages & EncoderSetBufferStage::Compute) {
      ASSERT(m3_state().compute_enc);
      m3_state().compute_enc->setBuffer(buffer, offset, bind_point);
    }
    if (stages & EncoderSetBufferStage::Vertex) {
      ASSERT(m3_state().render_enc);
      m3_state().render_enc->setVertexBuffer(buffer, offset, bind_point);
    }
    if (stages & EncoderSetBufferStage::Mesh) {
      ASSERT(m3_state().render_enc);
      m3_state().render_enc->setMeshBuffer(buffer, offset, bind_point);
    }
    if (stages & EncoderSetBufferStage::Object) {
      ASSERT(m3_state().render_enc);
      m3_state().render_enc->setObjectBuffer(buffer, offset, bind_point);
    }
    if (stages & EncoderSetBufferStage::Fragment) {
      ASSERT(m3_state().render_enc);
      m3_state().render_enc->setFragmentBuffer(buffer, offset, bind_point);
    }
  }
}
template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::begin_rendering(
    std::initializer_list<rhi::RenderingAttachmentInfo> attachments) {
  // TODO: consolidate
  NS::SharedPtr<MTL4::RenderPassDescriptor> m4_desc;
  NS::SharedPtr<MTL::RenderPassDescriptor> m3_desc;
  if constexpr (UseMTL4) {
    m4_desc = NS::TransferPtr(MTL4::RenderPassDescriptor::alloc()->init());
    size_t color_att_i = 0;
    NS::SharedPtr<MTL::RenderPassDepthAttachmentDescriptor> depth_desc;
    for (const auto& att : attachments) {
      if (att.type == rhi::RenderingAttachmentInfo::Type::DepthStencil) {
        depth_desc = NS::TransferPtr(MTL::RenderPassDepthAttachmentDescriptor::alloc()->init());
        depth_desc->setTexture(
            reinterpret_cast<MetalTexture*>(device_->get_tex(att.image))->texture());
        depth_desc->setLoadAction(mtl::util::convert(att.load_op));
        depth_desc->setStoreAction(mtl::util::convert(att.store_op));
        depth_desc->setClearDepth(att.clear_value.depth_stencil.depth);
        m4_desc->setDepthAttachment(depth_desc.get());
      } else {
        auto* color_desc = m4_desc->colorAttachments()->object(color_att_i);
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
  } else {
    m3_desc = NS::TransferPtr(MTL::RenderPassDescriptor::alloc()->init());
    size_t color_att_i = 0;
    NS::SharedPtr<MTL::RenderPassDepthAttachmentDescriptor> depth_desc;
    for (const auto& att : attachments) {
      if (att.type == rhi::RenderingAttachmentInfo::Type::DepthStencil) {
        depth_desc = NS::TransferPtr(MTL::RenderPassDepthAttachmentDescriptor::alloc()->init());
        depth_desc->setTexture(
            reinterpret_cast<MetalTexture*>(device_->get_tex(att.image))->texture());
        depth_desc->setLoadAction(mtl::util::convert(att.load_op));
        depth_desc->setStoreAction(mtl::util::convert(att.store_op));
        depth_desc->setClearDepth(att.clear_value.depth_stencil.depth);
        m3_desc->setDepthAttachment(depth_desc.get());
      } else {
        auto* color_desc = m3_desc->colorAttachments()->object(color_att_i);
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
  }

  end_blit_encoder();
  end_compute_encoder();
  if constexpr (UseMTL4) {
    m4_state().render_enc = m4_state().cmd_buf->renderCommandEncoder(m4_desc.get());
    m4_state().render_enc->setArgumentTable(m4_state().arg_table,
                                            MTL::RenderStageVertex | MTL::RenderStageFragment |
                                                MTL::RenderStageObject | MTL::RenderStageMesh);
    if (!curr_debug_name_.empty()) {
      m4_state().render_enc->setLabel(mtl::util::string(curr_debug_name_));
    }
  } else {
    m3_state().render_enc = m3_state().cmd_buf->renderCommandEncoder(m3_desc.get());
    if (!curr_debug_name_.empty()) {
      m3_state().render_enc->setLabel(mtl::util::string(curr_debug_name_));
    }
  }
  flush_barriers();
  set_buffer(kIRDescriptorHeapBindPoint, device_->get_mtl_buf(device_->resource_descriptor_table_),
             0,
             EncoderSetBufferStage::Vertex | EncoderSetBufferStage::Fragment |
                 EncoderSetBufferStage::Object | EncoderSetBufferStage::Mesh);
  set_buffer(kIRSamplerHeapBindPoint, device_->get_mtl_buf(device_->sampler_descriptor_table_), 0,
             EncoderSetBufferStage::Vertex | EncoderSetBufferStage::Fragment |
                 EncoderSetBufferStage::Object | EncoderSetBufferStage::Mesh);
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::reset(MetalDevice* device) {
  device_ = device;
  // TODO: refactor
  cmd_icb_mgr_.init(device_);
  if constexpr (UseMTL4) {
    ASSERT((!m4_state().render_enc && !m4_state().compute_enc));
  } else {
    ASSERT((!m3_state().render_enc && !m3_state().compute_enc && !m3_state().blit_enc));
  }
  root_layout_ = {};
  binding_table_ = {};

  if constexpr (UseMTL4) {
    if (!m4_state().arg_table) {
      auto desc = NS::TransferPtr(MTL4::ArgumentTableDescriptor::alloc()->init());
      desc->setInitializeBindings(false);
      desc->setMaxBufferBindCount(10);
      desc->setMaxSamplerStateBindCount(0);
      desc->setMaxTextureBindCount(0);
      NS::Error* err{};
      m4_state().arg_table = device_->get_device()->newArgumentTable(desc.get(), &err);
      ASSERT(!err);
    }
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::flush_barriers() {
  if constexpr (UseMTL4) {
    if (m4_state().compute_enc && device_->compute_enc_flush_stages_) {
      constexpr MTL4::VisibilityOptions visibility_options = MTL4::VisibilityOptionNone;
      m4_state().compute_enc->barrierAfterStages(MTL::StageAll, MTL::StageAll, visibility_options);
      device_->compute_enc_flush_stages_ = 0;
      device_->compute_enc_dst_stages_ = 0;
    }
    if (m4_state().render_enc && device_->render_enc_flush_stages_) {
      constexpr MTL4::VisibilityOptions visibility_options = MTL4::VisibilityOptionNone;
      m4_state().render_enc->barrierAfterStages(MTL::StageAll, MTL::StageAll, visibility_options);
      device_->render_enc_flush_stages_ = 0;
      device_->render_enc_dst_stages_ = 0;
    }
  } else {
    if (m3_state().compute_enc && device_->compute_enc_flush_stages_) {
      m3_state().compute_enc->barrierAfterQueueStages(device_->compute_enc_flush_stages_,
                                                      device_->compute_enc_dst_stages_);
      device_->compute_enc_flush_stages_ = 0;
      device_->compute_enc_dst_stages_ = 0;
    }
    if (m3_state().render_enc && device_->render_enc_flush_stages_) {
      m3_state().render_enc->barrierAfterQueueStages(device_->render_enc_flush_stages_,
                                                     device_->render_enc_dst_stages_);
      device_->render_enc_flush_stages_ = 0;
      device_->render_enc_dst_stages_ = 0;
    }
    if (m3_state().blit_enc && device_->blit_enc_flush_stages_) {
      m3_state().blit_enc->barrierAfterQueueStages(device_->blit_enc_flush_stages_,
                                                   device_->blit_enc_dst_stages_);
      device_->blit_enc_flush_stages_ = 0;
      device_->blit_enc_dst_stages_ = 0;
    }
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::end_encoding() {
  end_render_encoder();
  end_compute_encoder();
  end_blit_encoder();
  ASSERT(!done_);
  done_ = true;
  if constexpr (UseMTL4) {
    m4_state().cmd_buf->endCommandBuffer();
  }
  device_->end_command_list(this);
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::pre_dispatch() {
  flush_barriers();
  ASSERT(m4_state().compute_enc);
  if constexpr (UseMTL4) {
    ASSERT(m4_state().compute_enc);
    // if (m4_state().compute_enc) {
    //   ASSERT(!m4_state().render_enc);
    //   m4_state().compute_enc = m4_state().cmd_buf->computeCommandEncoder();
    //   m4_state().compute_enc->setArgumentTable(m4_state().arg_table);
    // }
  } else {
    if (!m3_state().compute_enc) {
      ASSERT((!m3_state().blit_enc && !m3_state().render_enc));
      m3_state().compute_enc = m3_state().cmd_buf->computeCommandEncoder();
    }
  }
}
template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::pre_blit() {
  flush_barriers();
  if constexpr (UseMTL4) {
    pre_dispatch();
  } else {
    if (!m3_state().blit_enc) {
      m3_state().blit_enc = m3_state().cmd_buf->blitCommandEncoder();
    }
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::bind_pipeline(rhi::PipelineHandle handle) {
  auto* pipeline = reinterpret_cast<MetalPipeline*>(device_->get_pipeline(handle));
  ASSERT(pipeline);
  ASSERT(pipeline->render_pso || pipeline->compute_pso);
  if (pipeline->render_pso) {
    if constexpr (UseMTL4) {
      ASSERT(m4_state().render_enc);
      m4_state().render_enc->setRenderPipelineState(pipeline->render_pso);
    } else {
      m3_state().render_enc->setRenderPipelineState(pipeline->render_pso);
    }
  } else if (pipeline->compute_pso) {
    end_render_encoder();
    if constexpr (UseMTL4) {
      if (!m4_state().compute_enc) {
        m4_state().compute_enc = m4_state().cmd_buf->computeCommandEncoder();
        m4_state().compute_enc->setArgumentTable(m4_state().arg_table);
        set_buffer(kIRDescriptorHeapBindPoint,
                   device_->get_mtl_buf(device_->resource_descriptor_table_), 0,
                   EncoderSetBufferStage::Compute);
        set_buffer(kIRSamplerHeapBindPoint,
                   device_->get_mtl_buf(device_->sampler_descriptor_table_), 0,
                   EncoderSetBufferStage::Compute);
      }
      m4_state().compute_enc->setComputePipelineState(pipeline->compute_pso);
    } else {
      end_blit_encoder();
      if (!m3_state().compute_enc) {
        m3_state().compute_enc = m3_state().cmd_buf->computeCommandEncoder();
      }
      m3_state().compute_enc->setComputePipelineState(pipeline->compute_pso);
    }
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::set_viewport(glm::uvec2 min, glm::uvec2 extent) {
  MTL::Viewport vp;
  vp.originX = min.x;
  vp.originY = min.y;
  vp.width = extent.x;
  vp.height = extent.y;
  vp.znear = 0.0f;
  vp.zfar = 1.f;
  if constexpr (UseMTL4) {
    m4_state().render_enc->setViewport(vp);
  } else {
    m3_state().render_enc->setViewport(vp);
  }
}
template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::set_scissor(glm::uvec2 min, glm::uvec2 extent) {
  MTL::ScissorRect r{.x = min.x, .y = min.y, .width = extent.x, .height = extent.y};
  if constexpr (UseMTL4) {
    m4_state().render_enc->setScissorRect(r);
  } else {
    m3_state().render_enc->setScissorRect(r);
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::draw_primitives(rhi::PrimitiveTopology topology,
                                                   size_t vertex_start, size_t count,
                                                   size_t instance_count) {
  flush_binds();
  if constexpr (UseMTL4) {
    m4_state().render_enc->drawPrimitives(mtl::util::convert(topology), vertex_start, count,
                                          instance_count);
  } else {
    m3_state().render_enc->drawPrimitives(mtl::util::convert(topology), vertex_start, count,
                                          instance_count);
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::push_constants(void* data, size_t size) {
  if constexpr (UseMTL4) {
    // TODO: address potential misbindings as a result of ICB management
  }
  ASSERT(size <= sizeof(root_layout_.constants) - 8);
  memcpy(root_layout_.constants, data, size);
  push_constant_dirty_ = true;
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::draw_indexed_primitives(
    rhi::PrimitiveTopology topology, rhi::BufferHandle index_buf, size_t index_start, size_t count,
    size_t instance_count, size_t base_vertex_idx, size_t base_instance,
    rhi::IndexType index_type) {
  root_layout_.constants[20] = base_instance;
  root_layout_.constants[21] = base_vertex_idx;
  flush_binds();
  auto* buf = device_->get_mtl_buf(index_buf);
  if constexpr (UseMTL4) {
    m4_state().render_enc->drawIndexedPrimitives(
        mtl::util::convert(topology), count,
        index_type == rhi::IndexType::Uint32 ? MTL::IndexTypeUInt32 : MTL::IndexTypeUInt16,
        buf->gpuAddress() + index_start, buf->length(), instance_count, 0, 0);
  } else {
    m3_state().render_enc->drawIndexedPrimitives(
        mtl::util::convert(topology), count,
        index_type == rhi::IndexType::Uint32 ? MTL::IndexTypeUInt32 : MTL::IndexTypeUInt16, buf,
        index_start, instance_count, 0, 0);
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::set_depth_stencil_state(rhi::CompareOp depth_compare_op,
                                                           bool depth_write_enabled) {
  // TODO: cache this pls
  auto depth_stencil_desc = NS::TransferPtr(MTL::DepthStencilDescriptor::alloc()->init());
  depth_stencil_desc->setDepthCompareFunction(mtl::util::convert(depth_compare_op));
  depth_stencil_desc->setDepthWriteEnabled(depth_write_enabled);
  if constexpr (UseMTL4) {
    m4_state().render_enc->setDepthStencilState(
        device_->get_device()->newDepthStencilState(depth_stencil_desc.get()));
  } else {
    m3_state().render_enc->setDepthStencilState(
        device_->get_device()->newDepthStencilState(depth_stencil_desc.get()));
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::set_wind_order(rhi::WindOrder wind_order) {
  if constexpr (UseMTL4) {
    m4_state().render_enc->setFrontFacingWinding(mtl::util::convert(wind_order));
  } else {
    m3_state().render_enc->setFrontFacingWinding(mtl::util::convert(wind_order));
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::set_cull_mode(rhi::CullMode cull_mode) {
  if constexpr (UseMTL4) {
    m4_state().render_enc->setCullMode(mtl::util::convert(cull_mode));
  } else {
    m3_state().render_enc->setCullMode(mtl::util::convert(cull_mode));
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::upload_texture_data(rhi::BufferHandle src_buf, size_t src_offset,
                                                       size_t src_bytes_per_row,
                                                       rhi::TextureHandle dst_tex) {
  upload_texture_data(src_buf, src_offset, src_bytes_per_row, dst_tex,
                      device_->get_tex(dst_tex)->desc().dims, glm::uvec3{0, 0, 0}, -1);
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::upload_texture_data(rhi::BufferHandle src_buf, size_t src_offset,
                                                       size_t src_bytes_per_row,
                                                       rhi::TextureHandle dst_tex,
                                                       glm::uvec3 src_size, glm::uvec3 dst_origin,
                                                       int mip_level) {
  start_blit_equivalent_encoder();
  auto* buf = device_->get_mtl_buf(src_buf);
  auto* tex = device_->get_mtl_tex(dst_tex);
  ALWAYS_ASSERT(buf);
  ALWAYS_ASSERT(tex);
  MTL::Size img_size = MTL::Size::Make(src_size.x, src_size.y, src_size.z);
  auto mip = mip_level < 0 ? 0 : mip_level;
  if constexpr (UseMTL4) {
    m4_state().compute_enc->copyFromBuffer(
        buf, src_offset, src_bytes_per_row, 0, img_size, tex, 0, mip,
        MTL::Origin::Make(dst_origin.x, dst_origin.y, dst_origin.z));
    m4_state().compute_enc->barrierAfterEncoderStages(MTL::StageBlit, MTL::StageBlit,
                                                      MTL4::VisibilityOptionDevice);
  } else {
    m3_state().blit_enc->copyFromBuffer(
        buf, src_offset, src_bytes_per_row, 0, img_size, tex, 0, mip,
        MTL::Origin::Make(dst_origin.x, dst_origin.y, dst_origin.z));
  }

  if constexpr (UseMTL4) {
    if (mip_level < 0 && tex->mipmapLevelCount() > 1) {
      m4_state().compute_enc->generateMipmaps(tex);
    }
    m4_state().compute_enc->barrierAfterEncoderStages(MTL::StageBlit, MTL::StageBlit,
                                                      MTL4::VisibilityOptionDevice);
  } else {
    if (mip_level < 0 && tex->mipmapLevelCount() > 1) {
      m3_state().blit_enc->generateMipmaps(tex);
    }
  }
}

template <bool UseMTL4>
uint32_t MetalCmdEncoderBase<UseMTL4>::prepare_indexed_indirect_draws(
    rhi::BufferHandle /*indirect_buf*/, size_t /*offset*/, size_t /*tot_draw_cnt*/,
    rhi::BufferHandle /*index_buf*/, size_t /*index_buf_offset*/, void* push_constant_data,
    size_t push_constant_size, size_t /*vertex_stride*/) {
  ASSERT(push_constant_size <= sizeof(RootLayout) - 8);
  auto [pc_buf, pc_buf_offset] = device_->push_constant_allocator_->alloc(sizeof(RootLayout));
  auto* root_layout_ptr = (RootLayout*)((uint8_t*)pc_buf->contents() + pc_buf_offset);
  memcpy(root_layout_ptr->constants, push_constant_data, push_constant_size);

  ASSERT(0 && " nooooooooooooo");
  // start_compute_encoder();

  // ASSERT(index_buf.is_valid());
  // auto* index_buffer = device_->get_mtl_buf(index_buf);
  // ASSERT(index_buffer);
  // set_buffer(5, index_buffer, index_buf_offset, EncoderSetBufferStage::Compute);
  //
  // set_buffer<EncoderAPI, typename EncoderAPI::ComputeEnc>(
  //     encoder_state_, encoder_state_.compute_enc, 6,
  //     device_->get_mtl_buf(device_->resource_descriptor_table_), 0,
  //     EncoderSetBufferStage::Compute);
  //
  // set_buffer<EncoderAPI, typename EncoderAPI::ComputeEnc>(
  //     encoder_state_, encoder_state_.compute_enc, 7,
  //     device_->get_mtl_buf(device_->sampler_descriptor_table_), 0,
  //     EncoderSetBufferStage::Compute);
  //
  // ASSERT(device_->get_psos().dispatch_indirect_pso);
  // ASSERT(device_->get_buf(indirect_buf)->desc().usage & rhi::BufferUsage_Indirect);
  //
  // auto [indirect_buf_id, icbs] = device_->icb_mgr_draw_indexed_.alloc(indirect_buf,
  // tot_draw_cnt);
  //
  // encoder_state_.compute_enc->setComputePipelineState(device_->get_psos().dispatch_indirect_pso);
  //
  // for (int i = 0, rem_draw_count = tot_draw_cnt; i < (int)icbs.size();
  //      i++, rem_draw_count -= k_max_draws_per_icb) {
  //   uint32_t draw_cnt = std::min<uint32_t>(k_max_draws_per_icb, rem_draw_count);
  //   auto* icb = icbs[i];
  //   ASSERT((icb && icb->size() == draw_cnt));
  //
  //   cmd_icb_mgr_.init_icb_arg_encoder_and_buf_and_set_icb(icbs, i);
  //
  //   set_buffer<EncoderAPI, typename EncoderAPI::ComputeEnc>(
  //       encoder_state_, encoder_state_.compute_enc, 0, pc_buf, pc_buf_offset,
  //       EncoderSetBufferStage::Compute);
  //
  //   struct Args2 {
  //     uint32_t draw_cnt;
  //     uint32_t stride;
  //   };
  //
  //   auto [args2_buf, args2_offset] = device_->test_allocator_->alloc(sizeof(Args2));
  //   auto* args2 = (Args2*)((uint8_t*)args2_buf->contents() + args2_offset);
  //   args2->draw_cnt = draw_cnt;
  //   args2->stride = vertex_stride;
  //
  //   set_buffer<EncoderAPI, typename EncoderAPI::ComputeEnc>(
  //       encoder_state_, encoder_state_.compute_enc, 1, args2_buf, args2_offset,
  //       EncoderSetBufferStage::Compute);
  //
  //   set_buffer<EncoderAPI, typename EncoderAPI::ComputeEnc>(
  //       encoder_state_, encoder_state_.compute_enc, 2, cmd_icb_mgr_.get_icb(i), 0,
  //       EncoderSetBufferStage::Compute);
  //
  //   auto [out_pc_arg_buf, out_pc_arg_buf_offset] =
  //       device_->test_allocator_->alloc(draw_cnt * sizeof(RootLayout));
  //
  //   set_buffer<EncoderAPI, typename EncoderAPI::ComputeEnc>(
  //       encoder_state_, encoder_state_.compute_enc, 3, out_pc_arg_buf, out_pc_arg_buf_offset,
  //       EncoderSetBufferStage::Compute);
  //
  //   auto* indirect_buffer = device_->get_mtl_buf(indirect_buf);
  //   ASSERT(indirect_buffer);
  //
  //   size_t iter_offset = i * k_max_draws_per_icb * sizeof(IndexedIndirectDrawCmd);
  //
  //   set_buffer<EncoderAPI, typename EncoderAPI::ComputeEnc>(
  //       encoder_state_, encoder_state_.compute_enc, 4, indirect_buffer, offset + iter_offset,
  //       EncoderSetBufferStage::Compute);
  //
  //   uint32_t threads_per_tg_x = 64;
  //   uint32_t tg_x = (draw_cnt + threads_per_tg_x - 1) / threads_per_tg_x;
  //   encoder_state_.compute_enc->dispatchThreadgroups(MTL::Size::Make(tg_x, 1, 1),
  //                                                    MTL::Size::Make(threads_per_tg_x, 1, 1));
  // }

  // barrier_after_queue_stages<UseMTL4>(encoder_state_.compute_enc, MTL::StageDispatch,
  // MTL::StageAll);
  //
  // return indirect_buf_id;
  return -1;
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::barrier(rhi::PipelineStage src_stage, rhi::AccessFlags,
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
  if constexpr (UseMTL4) {
    if (dst_mtl_stage & MTL::StageBlit) {
      device_->blit_enc_flush_stages_ |= src_mtl_stage;
      device_->blit_enc_dst_stages_ |= dst_mtl_stage;
    }
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::barrier(rhi::BufferHandle, rhi::PipelineStage src_stage,
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
  if constexpr (UseMTL4) {
    if (dst_mtl_stage & MTL::StageBlit) {
      device_->blit_enc_flush_stages_ |= src_mtl_stage;
      device_->blit_enc_dst_stages_ |= dst_mtl_stage;
    }
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::draw_indexed_indirect(rhi::BufferHandle indirect_buf,
                                                         uint32_t indirect_buf_id, size_t draw_cnt,
                                                         size_t offset_i) {
  flush_binds();
  ASSERT(indirect_buf.is_valid());
  const auto& icbs = device_->icb_mgr_draw_indexed_.get(indirect_buf, indirect_buf_id);
  size_t rem_draws = draw_cnt;
  for (size_t i = 0, off = 0; i < icbs.size() && off < draw_cnt; i++, off += k_max_draws_per_icb) {
    if constexpr (UseMTL4) {
      m4_state().render_enc->executeCommandsInBuffer(
          icbs[i], NS::Range::Make(offset_i, std::min<uint32_t>(k_max_draws_per_icb, rem_draws)));
    } else {
      m3_state().render_enc->executeCommandsInBuffer(
          icbs[i], NS::Range::Make(offset_i, std::min<uint32_t>(k_max_draws_per_icb, rem_draws)));
    }
    rem_draws -= k_max_draws_per_icb;
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::copy_tex_to_buf(rhi::TextureHandle src_tex, size_t src_slice,
                                                   size_t src_level, rhi::BufferHandle dst_buf,
                                                   size_t dst_offset) {
  start_blit_equivalent_encoder();
  auto* tex = device_->get_mtl_tex(src_tex);
  auto* buf = device_->get_mtl_buf(dst_buf);
  size_t bytes_per_row = tex->width() * 4;
  if constexpr (UseMTL4) {
    m4_state().compute_enc->copyFromTexture(
        tex, src_slice, src_level, MTL::Origin::Make(0, 0, 0),
        MTL::Size::Make(tex->width(), tex->height(), tex->depth()), buf, dst_offset, bytes_per_row,
        0);
  } else {
    m3_state().blit_enc->copyFromTexture(tex, src_slice, src_level, MTL::Origin::Make(0, 0, 0),
                                         MTL::Size::Make(tex->width(), tex->height(), tex->depth()),
                                         buf, dst_offset, bytes_per_row, 0);
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::draw_mesh_threadgroups(
    glm::uvec3 thread_groups, glm::uvec3 threads_per_task_thread_group,
    glm::uvec3 threads_per_mesh_thread_group) {
  flush_binds();
  if constexpr (UseMTL4) {
    m4_state().render_enc->drawMeshThreadgroups(
        MTL::Size::Make(thread_groups.x, thread_groups.y, thread_groups.z),
        MTL::Size::Make(threads_per_task_thread_group.x, threads_per_task_thread_group.y,
                        threads_per_task_thread_group.z),
        MTL::Size::Make(threads_per_mesh_thread_group.x, threads_per_mesh_thread_group.y,
                        threads_per_mesh_thread_group.z));
  } else {
    m3_state().render_enc->drawMeshThreadgroups(
        MTL::Size::Make(thread_groups.x, thread_groups.y, thread_groups.z),
        MTL::Size::Make(threads_per_task_thread_group.x, threads_per_task_thread_group.y,
                        threads_per_task_thread_group.z),
        MTL::Size::Make(threads_per_mesh_thread_group.x, threads_per_mesh_thread_group.y,
                        threads_per_mesh_thread_group.z));
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::pop_debug_group() {
  ALWAYS_ASSERT(push_debug_group_stack_size_ > 0);
  push_debug_group_stack_size_--;
  if constexpr (UseMTL4) {
    m4_state().cmd_buf->popDebugGroup();
  } else {
    m3_state().cmd_buf->popDebugGroup();
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::push_debug_group(const char* name) {
  if constexpr (UseMTL4) {
    m4_state().cmd_buf->pushDebugGroup(mtl::util::string(name));
  } else {
    m3_state().cmd_buf->pushDebugGroup(mtl::util::string(name));
  }
  push_debug_group_stack_size_++;
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::dispatch_compute(glm::uvec3 thread_groups,
                                                    glm::uvec3 threads_per_threadgroup) {
  flush_binds();
  pre_dispatch();
  // TODO: address misbindings of descriptor/sampler heap
  if constexpr (UseMTL4) {
    m4_state().compute_enc->dispatchThreadgroups(
        MTL::Size::Make(thread_groups.x, thread_groups.y, thread_groups.z),
        MTL::Size::Make(threads_per_threadgroup.x, threads_per_threadgroup.y,
                        threads_per_threadgroup.z));

  } else {
    m3_state().compute_enc->dispatchThreadgroups(
        MTL::Size::Make(thread_groups.x, thread_groups.y, thread_groups.z),
        MTL::Size::Make(threads_per_threadgroup.x, threads_per_threadgroup.y,
                        threads_per_threadgroup.z));
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::fill_buffer(rhi::BufferHandle handle, uint32_t offset_bytes,
                                               uint32_t size, uint32_t value) {
  auto* buf = device_->get_mtl_buf(handle);
  ASSERT(buf);
  start_blit_equivalent_encoder();
  if constexpr (UseMTL4) {
    m4_state().compute_enc->fillBuffer(buf, NS::Range::Make(offset_bytes, size), value);
  } else {
    m3_state().blit_enc->fillBuffer(buf, NS::Range::Make(offset_bytes, size), value);
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::draw_mesh_threadgroups_indirect(
    rhi::BufferHandle indirect_buf, size_t indirect_buf_offset,
    glm::uvec3 threads_per_task_thread_group, glm::uvec3 threads_per_mesh_thread_group) {
  flush_binds();
  auto* buf = device_->get_mtl_buf(indirect_buf);
  ASSERT(buf);
  if constexpr (UseMTL4) {
    m4_state().render_enc->drawMeshThreadgroups(
        buf->gpuAddress() + indirect_buf_offset,
        MTL::Size::Make(threads_per_task_thread_group.x, threads_per_task_thread_group.y,
                        threads_per_task_thread_group.z),
        MTL::Size::Make(threads_per_mesh_thread_group.x, threads_per_mesh_thread_group.y,
                        threads_per_mesh_thread_group.z));
  } else {
    m3_state().render_enc->drawMeshThreadgroups(
        buf, indirect_buf_offset,
        MTL::Size::Make(threads_per_task_thread_group.x, threads_per_task_thread_group.y,
                        threads_per_task_thread_group.z),
        MTL::Size::Make(threads_per_mesh_thread_group.x, threads_per_mesh_thread_group.y,
                        threads_per_mesh_thread_group.z));
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::copy_buffer_to_buffer(rhi::BufferHandle src_buf,
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
    if constexpr (UseMTL4) {
      m4_state().compute_enc->copyFromBuffer(src_b->buffer(), src_offset, dst_b->buffer(),
                                             dst_offset, size);
    } else {
      m3_state().blit_enc->copyFromBuffer(src_b->buffer(), src_offset, dst_b->buffer(), dst_offset,
                                          size);
    }
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::start_blit_equivalent_encoder() {
  if constexpr (UseMTL4) {
    start_compute_encoder();
  } else {
    start_blit_encoder();
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::set_debug_name(const char* name) {
  curr_debug_name_ = name;
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::end_rendering() {
  end_render_encoder();
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::flush_binds() {
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

  if constexpr (UseMTL4) {
    if (m4_state().compute_enc) {
      set_buffer(kIRArgumentBufferBindPoint, pc_buf, pc_buf_offset, EncoderSetBufferStage::Compute);
    } else {
      ASSERT(m4_state().render_enc);
      set_buffer(kIRArgumentBufferBindPoint, pc_buf, pc_buf_offset,
                 EncoderSetBufferStage::Vertex | EncoderSetBufferStage::Fragment |
                     EncoderSetBufferStage::Object | EncoderSetBufferStage::Mesh);
    }
  } else {
    if (m3_state().compute_enc) {
      set_buffer(kIRArgumentBufferBindPoint, pc_buf, pc_buf_offset, EncoderSetBufferStage::Compute);
    } else {
      ASSERT(m3_state().render_enc);
      set_buffer(kIRArgumentBufferBindPoint, pc_buf, pc_buf_offset,
                 EncoderSetBufferStage::Vertex | EncoderSetBufferStage::Fragment |
                     EncoderSetBufferStage::Object | EncoderSetBufferStage::Mesh);
    }
  }
  push_constant_dirty_ = false;
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::bind_srv(rhi::TextureHandle texture, uint32_t slot,
                                            int subresource_id) {
  ASSERT(slot < ARRAY_SIZE(binding_table_.SRV));
  binding_table_.SRV[slot] = texture.to64();
  binding_table_.SRV_subresources[slot] = subresource_id;
  binding_table_dirty_ = true;
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::bind_uav(rhi::TextureHandle texture, uint32_t slot,
                                            int subresource_id) {
  ASSERT(slot < ARRAY_SIZE(binding_table_.UAV));
  binding_table_.UAV[slot] = texture.to64();
  binding_table_.UAV_subresources[slot] = subresource_id;
  binding_table_dirty_ = true;
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::bind_cbv(rhi::BufferHandle buffer, uint32_t slot,
                                            size_t offset_bytes) {
  ASSERT(slot < ARRAY_SIZE(binding_table_.SRV));
  binding_table_.CBV[slot] = buffer;
  binding_table_.CBV_offsets[slot] = offset_bytes;
  binding_table_dirty_ = true;
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::bind_uav(rhi::BufferHandle buffer, uint32_t slot,
                                            size_t offset_bytes) {
  ASSERT(slot < ARRAY_SIZE(binding_table_.SRV));
  binding_table_.UAV[slot] = buffer.to64();
  binding_table_.UAV_subresources[slot] = DescriptorBindingTable::k_buffer_resource;
  binding_table_.UAV_offsets[slot] = offset_bytes;
  binding_table_dirty_ = true;
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::bind_srv(rhi::BufferHandle buffer, uint32_t slot,
                                            size_t offset_bytes) {
  ASSERT(slot < ARRAY_SIZE(binding_table_.SRV));
  binding_table_.SRV[slot] = buffer.to64();
  binding_table_.SRV_subresources[slot] = DescriptorBindingTable::k_buffer_resource;
  binding_table_.SRV_offsets[slot] = offset_bytes;
  binding_table_dirty_ = true;
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::write_timestamp(rhi::QueryPoolHandle query_pool,
                                                   uint32_t query_index) {
  if constexpr (UseMTL4) {
    auto* pool = (MetalQueryPool*)device_->get_query_pool(query_pool);
    if (m4_state().compute_enc) {
      m4_state().compute_enc->writeTimestamp(MTL4::TimestampGranularityPrecise, pool->heap_,
                                             query_index);
    } else if (m4_state().render_enc) {
      m4_state().render_enc->writeTimestamp(MTL4::TimestampGranularityPrecise,
                                            MTL::RenderStageFragment, pool->heap_, query_index);
    } else {
      m4_state().cmd_buf->writeTimestampIntoHeap(pool->heap_, query_index);
    }
  } else {
    LWARN("query pools not supported in Metal3");
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::set_label(const std::string& label) {
  if constexpr (UseMTL4) {
    m4_state().cmd_buf->setLabel(mtl::util::string(label.c_str()));
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::query_resolve(rhi::QueryPoolHandle query_pool,
                                                 uint32_t start_query, uint32_t query_count,
                                                 rhi::BufferHandle dst_buffer, size_t dst_offset) {
  if constexpr (UseMTL4) {
    auto* pool = (MetalQueryPool*)device_->get_query_pool(query_pool);
    auto* buf = device_->get_mtl_buf(dst_buffer);
    auto range =
        MTL4::BufferRange::Make(buf->gpuAddress() + dst_offset, query_count * sizeof(uint64_t));
    ASSERT(buf->length() >= dst_offset + query_count * sizeof(uint64_t));
    m4_state().cmd_buf->resolveCounterHeap(pool->heap_, NS::Range::Make(start_query, query_count),
                                           range, nullptr, nullptr);
  } else {
    LWARN("query pools not supported in Metal3");
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::end_compute_encoder() {
  if constexpr (UseMTL4) {
    if (m4_state().compute_enc) {
      m4_state().compute_enc->endEncoding();
      m4_state().compute_enc = nullptr;
    }
  } else {
    if (m3_state().compute_enc) {
      m3_state().compute_enc->endEncoding();
      m3_state().compute_enc = nullptr;
    }
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::end_blit_encoder() {
  if constexpr (!UseMTL4) {
    if (m3_state().blit_enc) {
      m3_state().blit_enc->endEncoding();
      m3_state().blit_enc = nullptr;
    }
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::end_render_encoder() {
  if constexpr (UseMTL4) {
    if (m4_state().render_enc) {
      m4_state().render_enc->endEncoding();
      m4_state().render_enc = nullptr;
    }
  } else {
    if (m3_state().render_enc) {
      m3_state().render_enc->endEncoding();
      m3_state().render_enc = nullptr;
    }
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::start_blit_encoder() {
  if constexpr (!UseMTL4) {
    if (!m3_state().blit_enc) {
      end_compute_encoder();
      end_render_encoder();
      m3_state().blit_enc = m3_state().cmd_buf->blitCommandEncoder();
    }
  }
}

template <bool UseMTL4>
void MetalCmdEncoderBase<UseMTL4>::start_compute_encoder() {
  if constexpr (UseMTL4) {
    if (!m4_state().compute_enc) {
      end_render_encoder();
      m4_state().compute_enc = m4_state().cmd_buf->computeCommandEncoder();
      m4_state().compute_enc->setArgumentTable(m4_state().arg_table);
    }
  } else {
    if (!m3_state().compute_enc) {
      end_render_encoder();
      end_blit_encoder();
      m3_state().compute_enc = m3_state().cmd_buf->computeCommandEncoder();
    }
  }
}

template class MetalCmdEncoderBase<true>;
template class MetalCmdEncoderBase<false>;
