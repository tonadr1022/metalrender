#include "Metal3CmdEncoder.hpp"

// clang-format off
#include <Metal/Metal.hpp>
#include <tracy/Tracy.hpp>
#include "gfx/metal/Config.hpp"
#include "hlsl/shared_indirect.h"
#define IR_RUNTIME_METALCPP
#include <metal_irconverter_runtime/metal_irconverter_runtime_wrapper.h>
// clang-format on

#include "gfx/metal/MetalCmdEncoderCommon.hpp"
#include "gfx/metal/MetalDevice.hpp"
#include "gfx/metal/MetalTexture.hpp"
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

void Metal3CmdEncoder::begin_rendering(
    std::initializer_list<rhi::RenderingAttachmentInfo> attachments) {
  MTL::RenderPassDescriptor* desc = MTL::RenderPassDescriptor::alloc()->init();

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

  end_encoders_of_types((EncoderType)(EncoderType_Blit | EncoderType_Compute));
  if (!render_enc_) {
    ASSERT(cmd_buf_);
    render_enc_ = cmd_buf_->renderCommandEncoder(desc);
  }
  render_enc_->setObjectBuffer(device_->get_mtl_buf(device_->resource_descriptor_table_), 0,
                               kIRDescriptorHeapBindPoint);
  render_enc_->setObjectBuffer(device_->get_mtl_buf(device_->sampler_descriptor_table_), 0,
                               kIRSamplerHeapBindPoint);
  render_enc_->setMeshBuffer(device_->get_mtl_buf(device_->resource_descriptor_table_), 0,
                             kIRDescriptorHeapBindPoint);
  render_enc_->setMeshBuffer(device_->get_mtl_buf(device_->sampler_descriptor_table_), 0,
                             kIRSamplerHeapBindPoint);
  render_enc_->setVertexBuffer(device_->get_mtl_buf(device_->resource_descriptor_table_), 0,
                               kIRDescriptorHeapBindPoint);
  render_enc_->setVertexBuffer(device_->get_mtl_buf(device_->sampler_descriptor_table_), 0,
                               kIRSamplerHeapBindPoint);
  render_enc_->setFragmentBuffer(device_->get_mtl_buf(device_->resource_descriptor_table_), 0,
                                 kIRDescriptorHeapBindPoint);
  render_enc_->setFragmentBuffer(device_->get_mtl_buf(device_->sampler_descriptor_table_), 0,
                                 kIRSamplerHeapBindPoint);

  desc->release();
  if (depth_desc) {
    depth_desc->release();
  }
}

void Metal3CmdEncoder::end_encoding() {
  if (compute_enc_) {
    compute_enc_->endEncoding();
    compute_enc_ = nullptr;
  }
  if (blit_enc_) {
    blit_enc_->endEncoding();
    blit_enc_ = nullptr;
  }
  if (render_enc_) {
    render_enc_->endEncoding();
    render_enc_ = nullptr;
  }
}

void Metal3CmdEncoder::bind_pipeline(rhi::PipelineHandle handle) {
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

void Metal3CmdEncoder::set_viewport(glm::uvec2 min, glm::uvec2 extent) {
  MTL::Viewport vp;
  vp.originX = min.x;
  vp.originY = min.y;
  vp.width = extent.x;
  vp.height = extent.y;
  vp.znear = 0.0f;
  vp.zfar = 1.f;
  render_enc_->setViewport(vp);
}

void Metal3CmdEncoder::set_scissor(glm::uvec2 min, glm::uvec2 extent) {
  MTL::ScissorRect r{.x = min.x, .y = min.y, .width = extent.x, .height = extent.y};
  render_enc_->setScissorRect(r);
}

void Metal3CmdEncoder::draw_primitives(rhi::PrimitiveTopology topology, size_t vertex_start,
                                       size_t count, size_t instance_count) {
  // TODO: this might not be based.
  render_enc_->drawPrimitives(mtl::util::convert(topology), vertex_start, count, instance_count);
}

void Metal3CmdEncoder::push_constants(void* data, size_t size) {
  ASSERT(size <= k_tlab_size);
  memcpy(pc_data_, data, size);
}

void Metal3CmdEncoder::draw_indexed_primitives(rhi::PrimitiveTopology topology,
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
  render_enc_->setVertexBuffer(pc_buf, pc_buf_offset, kIRArgumentBufferBindPoint);
  render_enc_->setFragmentBuffer(pc_buf, pc_buf_offset, kIRArgumentBufferBindPoint);
  render_enc_->drawIndexedPrimitives(
      mtl::util::convert(topology), count,
      index_type == rhi::IndexType::Uint32 ? MTL::IndexTypeUInt32 : MTL::IndexTypeUInt16, buf,
      index_start, instance_count, 0, 0);
}

void Metal3CmdEncoder::set_depth_stencil_state(rhi::CompareOp depth_compare_op,
                                               bool depth_write_enabled) {
  MTL::DepthStencilDescriptor* depth_stencil_desc = MTL::DepthStencilDescriptor::alloc()->init();
  depth_stencil_desc->setDepthCompareFunction(mtl::util::convert(depth_compare_op));
  depth_stencil_desc->setDepthWriteEnabled(depth_write_enabled);
  render_enc_->setDepthStencilState(
      device_->get_device()->newDepthStencilState(depth_stencil_desc));
}

void Metal3CmdEncoder::set_wind_order(rhi::WindOrder wind_order) {
  render_enc_->setFrontFacingWinding(mtl::util::convert(wind_order));
}

void Metal3CmdEncoder::set_cull_mode(rhi::CullMode cull_mode) {
  render_enc_->setCullMode(mtl::util::convert(cull_mode));
}

void Metal3CmdEncoder::upload_texture_data(rhi::BufferHandle src_buf, size_t src_offset,
                                           size_t src_bytes_per_row, rhi::TextureHandle dst_tex) {
  upload_texture_data(src_buf, src_offset, src_bytes_per_row, dst_tex,
                      device_->get_tex(dst_tex)->desc().dims, glm::uvec3{0, 0, 0});
}

void Metal3CmdEncoder::upload_texture_data(rhi::BufferHandle src_buf, size_t src_offset,
                                           size_t src_bytes_per_row, rhi::TextureHandle dst_tex,
                                           glm::uvec3 src_size, glm::uvec3 dst_origin) {
  start_blit_encoder();
  auto* buf = device_->get_mtl_buf(src_buf);
  auto* tex = device_->get_mtl_tex(dst_tex);
  ALWAYS_ASSERT(buf);
  ALWAYS_ASSERT(tex);
  MTL::Size img_size = MTL::Size::Make(src_size.x, src_size.y, src_size.z);
  ALWAYS_ASSERT(img_size.width * img_size.depth * img_size.height * 4 <=
                device_->get_buf(src_buf)->size());
  blit_enc_->copyFromBuffer(buf, src_offset, src_bytes_per_row, 0, img_size, tex, 0, 0,
                            MTL::Origin::Make(dst_origin.x, dst_origin.y, dst_origin.z));
  if (tex->mipmapLevelCount() > 1) {
    blit_enc_->generateMipmaps(tex);
  }
}

void Metal3CmdEncoder::copy_tex_to_buf(rhi::TextureHandle src_tex, size_t src_slice,
                                       size_t src_level, rhi::BufferHandle dst_buf,
                                       size_t dst_offset) {
  start_blit_encoder();
  auto* tex = device_->get_mtl_tex(src_tex);
  auto* buf = device_->get_mtl_buf(dst_buf);
  size_t bytes_per_row = tex->width() * 4;
  blit_enc_->copyFromTexture(tex, src_slice, src_level, MTL::Origin::Make(0, 0, 0),
                             MTL::Size::Make(tex->width(), tex->height(), tex->depth()), buf,
                             dst_offset, bytes_per_row, 0);
}

uint32_t Metal3CmdEncoder::prepare_indexed_indirect_draws(
    rhi::BufferHandle indirect_buf, size_t offset, size_t tot_draw_cnt, rhi::BufferHandle index_buf,
    size_t index_buf_offset, void* push_constant_data, size_t push_constant_size) {
  ALWAYS_ASSERT(tot_draw_cnt > 0);
  auto [pc_buf, pc_buf_offset] = device_->push_constant_allocator_->alloc(k_tlab_size);
  memcpy((uint8_t*)pc_buf->contents() + pc_buf_offset, push_constant_data, push_constant_size);

  start_compute_encoder();

  auto* index_buffer = device_->get_mtl_buf(index_buf);
  ASSERT(index_buffer);
  compute_enc_->setBuffer(index_buffer, index_buf_offset, 5);

  compute_enc_->setBuffer(device_->get_mtl_buf(device_->resource_descriptor_table_), 0, 6);
  compute_enc_->setBuffer(device_->get_mtl_buf(device_->sampler_descriptor_table_), 0, 7);

  ASSERT(device_->get_psos().dispatch_indirect_pso);
  ASSERT(device_->get_buf(indirect_buf)->desc().usage & rhi::BufferUsage_Indirect);

  compute_enc_->setComputePipelineState(device_->get_psos().dispatch_indirect_pso);

  auto [indirect_buf_id, icbs] = device_->icb_mgr_draw_indexed_.alloc(indirect_buf, tot_draw_cnt);
  for (size_t i = 0, rem_draw_count = tot_draw_cnt; i < icbs.size(); i++, rem_draw_count -= 1000) {
    uint32_t draw_cnt = std::min<uint32_t>(1000, rem_draw_count);
    auto* icb = icbs[i];
    ASSERT((icb && icb->size() == draw_cnt));

    cmd_icb_mgr_.init_icb_arg_encoder_and_buf_and_set_icb(icbs, i);

    compute_enc_->setBuffer(pc_buf, pc_buf_offset, 0);

    struct Args2 {
      uint32_t draw_cnt;
    };

    auto [args2_buf, args2_offset] = device_->test_allocator_->alloc(sizeof(Args2));
    auto* args2 = (Args2*)((uint8_t*)args2_buf->contents() + args2_offset);
    args2->draw_cnt = draw_cnt;
    compute_enc_->setBuffer(args2_buf, args2_offset, 1);

    compute_enc_->setBuffer(cmd_icb_mgr_.get_icb(i), 0, 2);

    auto [out_pc_arg_buf, out_pc_arg_buf_offset] =
        device_->test_allocator_->alloc(draw_cnt * sizeof(TLAB_Layout));
    compute_enc_->setBuffer(out_pc_arg_buf, out_pc_arg_buf_offset, 3);

    auto* indirect_buffer = device_->get_mtl_buf(indirect_buf);
    ASSERT(indirect_buffer);
    size_t iter_offset = i * k_max_draws_per_icb * sizeof(IndexedIndirectDrawCmd);
    compute_enc_->setBuffer(indirect_buffer, offset + iter_offset, 4);

    // TODO:  combine with mtl 4 logic pls
    uint32_t threads_per_tg_x = 64;
    uint32_t tg_x = (draw_cnt + threads_per_tg_x - 1) / threads_per_tg_x;
    compute_enc_->dispatchThreadgroups(MTL::Size::Make(tg_x, 1, 1),
                                       MTL::Size::Make(threads_per_tg_x, 1, 1));
  }
  return indirect_buf_id;
}

void Metal3CmdEncoder::prepare_mesh_threadgroups_indirect(
    rhi::BufferHandle mesh_cmd_indirect_buf, size_t mesh_cmd_indirect_buf_offset,
    glm::uvec3 threads_per_task_thread_group, glm::uvec3 threads_per_mesh_thread_group,
    void* push_constant_data, size_t push_constant_size, uint32_t total_draw_cnt) {
  ZoneScoped;
  exit(1);
  start_compute_encoder();
  compute_enc_->setComputePipelineState(device_->get_psos().dispatch_mesh_pso);
  compute_enc_->setBuffer(device_->get_mtl_buf(device_->resource_descriptor_table_), 0, 4);
  compute_enc_->setBuffer(device_->get_mtl_buf(device_->sampler_descriptor_table_), 0, 5);
  // The TLAB layout is only push constants
  auto [pc_buf, pc_buf_offset] = device_->push_constant_allocator_->alloc(k_tlab_size);
  memcpy((uint8_t*)pc_buf->contents() + pc_buf_offset, push_constant_data, push_constant_size);
  compute_enc_->setBuffer(pc_buf, pc_buf_offset, 2);

  // allocate icb
  auto [indirect_buf_id, icbs] =
      device_->icb_mgr_draw_mesh_threadgroups_.alloc(mesh_cmd_indirect_buf, total_draw_cnt);
  uint32_t prev_draw_count = UINT32_MAX;

  for (uint32_t i = 0, rem_draw_count = total_draw_cnt; i < icbs.size();
       i++, rem_draw_count -= 1000) {
    uint32_t draw_cnt = std::min<uint32_t>(1000, rem_draw_count);

    cmd_icb_mgr_.init_icb_arg_encoder_and_buf_and_set_icb(icbs, i);
    compute_enc_->setBuffer(cmd_icb_mgr_.get_icb(i), 0, 0);

    if (draw_cnt != prev_draw_count) {
      struct Args2 {
        glm::uvec3 threads_per_object_thread_group;
        glm::uvec3 threads_per_mesh_thread_group;
        uint32_t draw_cnt;
      };

      prev_draw_count = draw_cnt;
      auto [args2_buf, args2_offset] = device_->test_allocator_->alloc(sizeof(Args2));
      auto* args2 = (Args2*)((uint8_t*)args2_buf->contents() + args2_offset);
      args2->threads_per_object_thread_group = threads_per_task_thread_group;
      args2->threads_per_mesh_thread_group = threads_per_mesh_thread_group;
      args2->draw_cnt = draw_cnt;
      compute_enc_->setBuffer(args2_buf, args2_offset, 1);
    }

    auto* mesh_args_indirect_buf = device_->get_mtl_buf(mesh_cmd_indirect_buf);
    ASSERT(mesh_args_indirect_buf);
    exit(1);  // TODO: either pass the stride or use something real here
    size_t iter_offset = 1000 * sizeof(IndexedIndirectDrawCmd) * i;
    compute_enc_->setBuffer(mesh_args_indirect_buf, mesh_cmd_indirect_buf_offset + iter_offset, 3);
  }
}

void Metal3CmdEncoder::barrier(rhi::PipelineStage src_stage, rhi::AccessFlags,
                               rhi::PipelineStage dst_stage, rhi::AccessFlags) {
  // TODO: fence or something else to enable concurrent anywhere?
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

void Metal3CmdEncoder::draw_indexed_indirect(rhi::BufferHandle indirect_buf,
                                             uint32_t indirect_buf_id, size_t draw_cnt,
                                             size_t offset_i) {
  ASSERT(render_enc_);
  ASSERT(indirect_buf.is_valid());
  const auto& icbs = device_->icb_mgr_draw_indexed_.get(indirect_buf, indirect_buf_id);
  size_t rem_draws = draw_cnt;
  for (size_t i = 0, off = 0; i < icbs.size() && off < draw_cnt; i++, off += k_max_draws_per_icb) {
    render_enc_->executeCommandsInBuffer(
        icbs[i], NS::Range::Make(offset_i, std::min<uint32_t>(k_max_draws_per_icb, rem_draws)));
    rem_draws -= k_max_draws_per_icb;
  }
}

void Metal3CmdEncoder::draw_mesh_threadgroups_indirect(rhi::BufferHandle /*indirect_buf*/,
                                                       uint32_t /*indirect_buf_id*/,
                                                       size_t /*draw_cnt*/) {
  ZoneScoped;
  exit(1);
  ASSERT(render_enc_);
  // render_enc_->executeCommandsInBuffer(
  //     device_->icb_mgr_draw_mesh_threadgroups_.get(indirect_buf, indirect_buf_id),
  //     NS::Range::Make(0, draw_cnt));
}

Metal3CmdEncoder::Metal3CmdEncoder(MetalDevice* device, MTL::CommandBuffer* cmd_buf)
    : cmd_buf_(cmd_buf), device_(device), cmd_icb_mgr_(device_) {}

void Metal3CmdEncoder::end_encoders_of_types(EncoderType types) {
  if (types & EncoderType_Blit) {
    if (blit_enc_) {
      blit_enc_->endEncoding();
      blit_enc_ = nullptr;
    }
  }
  if (types & EncoderType_Compute) {
    if (compute_enc_) {
      compute_enc_->endEncoding();
      compute_enc_ = nullptr;
    }
  }
  if (types & EncoderType_Render) {
    if (render_enc_) {
      render_enc_->endEncoding();
      render_enc_ = nullptr;
    }
  }
}

void Metal3CmdEncoder::start_compute_encoder() {
  end_encoders_of_types((EncoderType)(EncoderType_Blit | EncoderType_Render));
  if (!compute_enc_) {
    // TODO: re-evaluate dispatch type
    compute_enc_ = cmd_buf_->computeCommandEncoder(MTL::DispatchTypeSerial);
  }
}

void Metal3CmdEncoder::start_blit_encoder() {
  end_encoders_of_types((EncoderType)(EncoderType_Compute | EncoderType_Render));
  if (!blit_enc_) {
    blit_enc_ = cmd_buf_->blitCommandEncoder();
  }
}

void Metal3CmdEncoder::draw_mesh_threadgroups(glm::uvec3 thread_groups,
                                              glm::uvec3 threads_per_task_thread_group,
                                              glm::uvec3 threads_per_mesh_thread_group) {
  auto [pc_buf, pc_buf_offset] = device_->push_constant_allocator_->alloc(k_tlab_size);
  auto* tlab = reinterpret_cast<TLAB_Layout*>((uint8_t*)pc_buf->contents() + pc_buf_offset);
  memcpy(tlab->pc_data, pc_data_, k_pc_size);
  tlab->cbuffer2.draw_id = 0;  // TODO: don't use this root signature
  tlab->cbuffer2.vertex_id_base = 0;
  render_enc_->setObjectBuffer(pc_buf, pc_buf_offset, kIRArgumentBufferBindPoint);
  render_enc_->setMeshBuffer(pc_buf, pc_buf_offset, kIRArgumentBufferBindPoint);
  render_enc_->setFragmentBuffer(pc_buf, pc_buf_offset, kIRArgumentBufferBindPoint);
  render_enc_->drawMeshThreadgroups(
      MTL::Size::Make(thread_groups.x, thread_groups.y, thread_groups.z),
      MTL::Size::Make(threads_per_task_thread_group.x, threads_per_task_thread_group.y,
                      threads_per_task_thread_group.z),
      MTL::Size::Make(threads_per_mesh_thread_group.x, threads_per_mesh_thread_group.y,
                      threads_per_mesh_thread_group.z));
}

void Metal3CmdEncoder::dispatch_compute(glm::uvec3 thread_groups,
                                        glm::uvec3 threads_per_threadgroup) {
  start_compute_encoder();
  // TODO: only do this if needed!
  auto [pc_buf, pc_buf_offset] = device_->push_constant_allocator_->alloc(160);
  auto* tlab = reinterpret_cast<TLAB_Layout*>((uint8_t*)pc_buf->contents() + pc_buf_offset);
  memcpy(tlab->pc_data, &pc_data_, sizeof(tlab->pc_data));

  compute_enc_->setBuffer(device_->get_mtl_buf(device_->resource_descriptor_table_), 0, 0);
  compute_enc_->setBuffer(device_->get_mtl_buf(device_->sampler_descriptor_table_), 0, 1);
  compute_enc_->setBuffer(pc_buf, pc_buf_offset, 2);

  compute_enc_->dispatchThreadgroups(
      MTL::Size::Make(thread_groups.x, thread_groups.y, thread_groups.z),
      MTL::Size::Make(threads_per_threadgroup.x, threads_per_threadgroup.y,
                      threads_per_threadgroup.z));
}

void Metal3CmdEncoder::fill_buffer(rhi::BufferHandle handle, uint32_t offset_bytes, uint32_t size,
                                   uint32_t value) {
  auto* buf = device_->get_mtl_buf(handle);
  ASSERT(buf);
  start_blit_encoder();
  blit_enc_->fillBuffer(buf, NS::Range::Make(offset_bytes, size), value);
}

void Metal3CmdEncoder::flush_compute_barriers() {
  if (compute_enc_ && compute_enc_flush_stages_) {
    compute_enc_->barrierAfterQueueStages(compute_enc_flush_stages_, compute_enc_dst_stages_);
  }
}

void Metal3CmdEncoder::flush_render_barriers() {
  if (render_enc_ && render_enc_flush_stages_) {
    render_enc_->barrierAfterQueueStages(render_enc_flush_stages_, render_enc_dst_stages_);
    render_enc_flush_stages_ = 0;
    render_enc_dst_stages_ = 0;
  }
}
