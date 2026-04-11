#include "RenderGraph.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <tracy/Tracy.hpp>
#include <utility>

#include "core/Config.hpp"
#include "core/EAssert.hpp"
#include "gfx/rhi/Buffer.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "gfx/rhi/Swapchain.hpp"
#include "gfx/rhi/Texture.hpp"
#include "small_vector/small_vector.hpp"

namespace TENG_NAMESPACE {

namespace gfx {

const char* to_string(RGResourceType type) {
  switch (type) {
    case RGResourceType::Texture:
      return "Texture";
    case RGResourceType::Buffer:
      return "Buffer";
    case RGResourceType::ExternalTexture:
      return "ExternalTexture";
    case RGResourceType::ExternalBuffer:
      return "ExternalBuffer";
  }
}

RenderGraph::Pass::Pass(NameId name_id, RenderGraph* rg, uint32_t pass_i, RGPassType type)
    : rg_(rg), pass_i_(pass_i), name_id_(name_id), type_(type) {}

RenderGraph::Pass& RenderGraph::add_pass(std::string_view name, RGPassType type) {
  auto idx = static_cast<uint32_t>(passes_.size());
  const NameId name_id = intern_name(name);
  passes_.emplace_back(name_id, this, idx, type);
  return passes_.back();
}

void RenderGraph::execute() {
  rhi::CmdEncoder* enc = device_->begin_cmd_encoder();
  execute(enc);
  enc->end_encoding();
}

void RenderGraph::execute(rhi::CmdEncoder* enc) {
  ZoneScoped;
  gch::small_vector<rhi::GPUBarrier, 8> post_pass_barriers;
  for (auto pass_i : pass_stack_) {
    post_pass_barriers.clear();
    auto& pass = passes_[pass_i];
    ZoneScopedN("Execute Pass");
    for (auto& barrier : pass_barrier_infos_[pass_i]) {
      if (barrier.resource.type == RGResourceType::Buffer ||
          barrier.resource.type == RGResourceType::ExternalBuffer) {
        auto buf_handle = barrier.resource.type == RGResourceType::Buffer
                              ? get_buf(barrier.resource)
                              : get_external_buf(barrier.resource);
        enc->barrier(buf_handle, barrier.src_state.stage, barrier.src_state.access,
                     barrier.dst_state.stage, barrier.dst_state.access);
      } else {
        auto tex_handle = barrier.resource.type == RGResourceType::Texture
                              ? get_att_img(barrier.resource)
                              : get_external_tex(barrier.resource);

        if (barrier.is_swapchain_write) {
          tex_handle = pass.swapchain_write_->get_current_texture();
          ASSERT(pass.swapchain_write_);
          post_pass_barriers.emplace_back(rhi::GPUBarrier::tex_barrier(
              pass.swapchain_write_->get_current_texture(), rhi::ResourceState::ColorWrite,
              rhi::ResourceState::SwapchainPresent));
        }
        enc->barrier(tex_handle, barrier.src_state.stage, barrier.src_state.access,
                     barrier.dst_state.stage, barrier.dst_state.access, barrier.src_state.layout,
                     barrier.dst_state.layout, barrier.subresource_mip, barrier.subresource_slice);
      }
    }

    enc->set_debug_name(pass.get_name().c_str());
    {
      ZoneScopedN("Execute Fn");
      pass.get_execute_fn()(enc);
    }
    for (auto& b : post_pass_barriers) {
      enc->barrier(&b);
    }
  }

  {
    passes_.clear();
    for (auto& b : pass_barrier_infos_) {
      b.clear();
    }
    external_texture_count_ = 0;
  }

  for (size_t i = 0; i < tex_att_infos_.size(); i++) {
    const rhi::TextureUsage usage = device_->get_tex(tex_att_handles_[i])->desc().usage;
    free_atts_[TexPoolKey{tex_att_infos_[i], usage}].emplace_back(tex_att_handles_[i]);
  }
  // Deferred-pool buffers from the prior execute: safe to merge into the free list now.
  for (auto& [key, bufs] : defer_pool_pending_return_) {
    for (auto& buf : bufs) {
      const rhi::BufferUsage usage = device_->get_buf(buf)->desc().usage;
      free_bufs_[BufPoolKey{key.info, usage}].emplace_back(buf);
    }
  }
  defer_pool_pending_return_.clear();

  for (size_t i = 0; i < buffer_infos_.size(); i++) {
    if (defer_pool_handles_by_slot_[i].is_valid()) {
      const rhi::BufferUsage usage = device_->get_buf(defer_pool_handles_by_slot_[i])->desc().usage;
      defer_pool_pending_return_[BufPoolKey{buffer_infos_[i], usage}].emplace_back(
          defer_pool_handles_by_slot_[i]);
    } else {
      const rhi::BufferUsage usage = device_->get_buf(buffer_handles_[i])->desc().usage;
      free_bufs_[BufPoolKey{buffer_infos_[i], usage}].emplace_back(buffer_handles_[i]);
    }
  }
  tex_att_infos_.clear();
  buffer_infos_.clear();
  resources_.clear();
  resource_use_id_to_writer_pass_idx_.clear();
  external_read_ids_.clear();
  external_buffers_.clear();
  external_textures_.clear();
  external_tex_handle_to_id_.clear();
  external_buf_handle_to_id_.clear();
  rg_id_to_external_texture_.clear();
  rg_id_to_external_buffer_.clear();
  curr_submitted_swapchain_textures_.clear();
  external_initial_states_.clear();
}

RenderGraph::NameId RenderGraph::intern_name(std::string_view name) {
  auto it = name_to_id_.find(name);
  if (it != name_to_id_.end()) {
    return it->second;
  }
  const auto id = static_cast<NameId>(id_to_name_.size());
  id_to_name_.emplace_back(name);
  name_to_id_.emplace(id_to_name_.back(), id);
  return id;
}

const std::string& RenderGraph::debug_name(NameId name) const {
  static const std::string kInvalidName = "<invalid>";
  if (name == kInvalidNameId || name >= id_to_name_.size()) {
    return kInvalidName;
  }
  return id_to_name_[name];
}

std::string RenderGraph::debug_name(RGResourceId id) const {
  ASSERT(id.is_valid());
  if (!id.is_valid() || id.idx >= resources_.size()) {
    return "<invalid>";
  }
  const auto& rec = resources_[id.idx];
  const auto& base = debug_name(rec.debug_name);
  if (id.version == 0) {
    return base;
  }
  return base + "#" + std::to_string(id.version);
}

void RenderGraph::init(rhi::Device* device) {
  device_ = device;
  passes_.reserve(200);
}

RGResourceId RenderGraph::create_texture(const AttachmentInfo& att_info,
                                         std::string_view debug_name) {
  ASSERT((att_info.is_swapchain_tex || att_info.format != rhi::TextureFormat::Undefined));
  RGResourceHandle handle = {.idx = static_cast<uint32_t>(tex_att_infos_.size()),
                             .type = RGResourceType::Texture};
  tex_att_infos_.emplace_back(att_info);

  auto record = create_resource_record(RGResourceType::Texture, handle.idx, debug_name);
  resources_.push_back(record);
  return RGResourceId{.idx = static_cast<uint32_t>(resources_.size() - 1),
                      .type = RGResourceType::Texture,
                      .version = 0};
}

RGResourceId RenderGraph::create_buffer(const BufferInfo& buf_info, std::string_view debug_name) {
  RGResourceHandle handle = {.idx = static_cast<uint32_t>(buffer_infos_.size()),
                             .type = RGResourceType::Buffer};
  buffer_infos_.emplace_back(buf_info);

  auto record = create_resource_record(RGResourceType::Buffer, handle.idx, debug_name);
  resources_.push_back(record);
  return RGResourceId{.idx = static_cast<uint32_t>(resources_.size() - 1),
                      .type = RGResourceType::Buffer,
                      .version = 0};
}

RGResourceId RenderGraph::import_external_texture(rhi::TextureHandle tex_handle,
                                                  std::string_view debug_name) {
  return import_external_texture(tex_handle, RGState{}, debug_name);
}

RGResourceId RenderGraph::import_external_texture(rhi::TextureHandle tex_handle,
                                                  const RGState& initial,
                                                  std::string_view debug_name) {
  const auto key = tex_handle.to64();
  if (auto it = external_tex_handle_to_id_.find(key); it != external_tex_handle_to_id_.end()) {
    if (!debug_name.empty()) {
      auto& rec = resources_[it->second.idx];
      if (rec.debug_name == kInvalidNameId) {
        rec.debug_name = intern_name(debug_name);
      }
    }
    const auto& rec = resources_[it->second.idx];
    RGResourceId id{.idx = it->second.idx,
                    .type = RGResourceType::ExternalTexture,
                    .version = rec.latest_version};
    external_initial_states_[get_physical_handle(id).to64()] = initial;
    rg_id_to_external_texture_[id] = tex_handle;
    return id;
  }
  auto idx = external_textures_.size();
  external_textures_.emplace_back(tex_handle);
  auto record = create_resource_record(RGResourceType::ExternalTexture, idx, debug_name);
  resources_.push_back(record);
  RGResourceId id{.idx = static_cast<uint32_t>(resources_.size() - 1),
                  .type = RGResourceType::ExternalTexture,
                  .version = 0};
  external_initial_states_[RGResourceHandle{.idx = static_cast<uint32_t>(idx),
                                            .type = RGResourceType::ExternalTexture}
                               .to64()] = initial;
  external_tex_handle_to_id_.emplace(key, id);
  rg_id_to_external_texture_[id] = tex_handle;
  return id;
}

RGResourceId RenderGraph::import_external_buffer(rhi::BufferHandle buf_handle,
                                                 std::string_view debug_name) {
  return import_external_buffer(buf_handle, RGState{}, debug_name);
}

RGResourceId RenderGraph::import_external_buffer(rhi::BufferHandle buf_handle,
                                                 const RGState& initial,
                                                 std::string_view debug_name) {
  const auto key = buf_handle.to64();
  if (auto it = external_buf_handle_to_id_.find(key); it != external_buf_handle_to_id_.end()) {
    if (!debug_name.empty()) {
      auto& rec = resources_[it->second.idx];
      if (rec.debug_name == kInvalidNameId) {
        rec.debug_name = intern_name(debug_name);
      }
    }
    const auto& rec = resources_[it->second.idx];
    RGResourceId id{.idx = it->second.idx,
                    .type = RGResourceType::ExternalBuffer,
                    .version = rec.latest_version};
    external_initial_states_[get_physical_handle(id).to64()] = initial;
    rg_id_to_external_buffer_[id] = buf_handle;
    return id;
  }
  auto idx = external_buffers_.size();
  external_buffers_.emplace_back(buf_handle);
  auto record = create_resource_record(RGResourceType::ExternalBuffer, idx, debug_name);
  resources_.push_back(record);
  RGResourceId id{.idx = static_cast<uint32_t>(resources_.size() - 1),
                  .type = RGResourceType::ExternalBuffer,
                  .version = 0};
  external_initial_states_[RGResourceHandle{.idx = static_cast<uint32_t>(idx),
                                            .type = RGResourceType::ExternalBuffer}
                               .to64()] = initial;
  external_buf_handle_to_id_.emplace(key, id);
  rg_id_to_external_buffer_[id] = buf_handle;
  return id;
}

AttachmentInfo* RenderGraph::get_tex_att_info(RGResourceId id) {
  return get_tex_att_info(get_physical_handle(id));
}

AttachmentInfo* RenderGraph::get_tex_att_info(RGResourceHandle handle) {
  ALWAYS_ASSERT(handle.type == RGResourceType::Texture);
  ALWAYS_ASSERT(handle.idx < tex_att_infos_.size());
  return &tex_att_infos_[handle.idx];
}

rhi::TextureHandle RenderGraph::get_att_img(RGResourceId tex_id) const {
  return get_att_img(get_physical_handle(tex_id));
}

rhi::TextureHandle RenderGraph::get_att_img(RGResourceHandle tex_handle) const {
  ASSERT(tex_handle.idx < tex_att_handles_.size());
  ASSERT(tex_handle.type == RGResourceType::Texture);
  return tex_att_handles_[tex_handle.idx];
}

rhi::BufferHandle RenderGraph::get_buf(RGResourceId buf_id) const {
  return get_buf(get_physical_handle(buf_id));
}

rhi::BufferHandle RenderGraph::get_buf(RGResourceHandle buf_handle) const {
  ASSERT(buf_handle.idx < buffer_handles_.size());
  ASSERT(buf_handle.type == RGResourceType::Buffer);
  return buffer_handles_[buf_handle.idx];
}

rhi::TextureHandle RenderGraph::get_external_texture(RGResourceId id) const {
  ALWAYS_ASSERT(id.type == RGResourceType::ExternalTexture);
  ALWAYS_ASSERT(id.idx < resources_.size());
  auto it = rg_id_to_external_texture_.find(id);
  ALWAYS_ASSERT(it != rg_id_to_external_texture_.end());
  return it->second;
}

rhi::BufferHandle RenderGraph::get_external_buffer(RGResourceId id) const {
  ALWAYS_ASSERT(id.type == RGResourceType::ExternalBuffer);
  ALWAYS_ASSERT(id.idx < resources_.size());
  auto it = rg_id_to_external_buffer_.find(id);
  ALWAYS_ASSERT(it != rg_id_to_external_buffer_.end());
  return it->second;
}

RGResourceHandle RenderGraph::get_physical_handle(RGResourceId id) const {
  ALWAYS_ASSERT(id.idx < resources_.size());
  const auto& rec = resources_[id.idx];
  ALWAYS_ASSERT(rec.type == id.type);
  return RGResourceHandle{.idx = rec.physical_idx, .type = rec.type};
}

void RenderGraph::register_write(RGResourceId id, RGPass& pass) {
  resource_use_id_to_writer_pass_idx_[id] = pass.get_idx();
}

RGResourceId RenderGraph::next_version(RGResourceId id) {
  ALWAYS_ASSERT(id.idx < resources_.size());
  auto& rec = resources_[id.idx];
  ALWAYS_ASSERT(rec.type == id.type);
  uint32_t next = std::max(rec.latest_version, id.version) + 1;
  rec.latest_version = next;
  return RGResourceId{.idx = id.idx, .type = id.type, .version = next};
}

RGResourceId RGPass::sample_tex(RGResourceId id) {
  rhi::PipelineStage stage{};
  if (type_ == RGPassType::Compute) {
    stage = rhi::PipelineStage::ComputeShader;
  } else if (type_ == RGPassType::Graphics) {
    stage = rhi::PipelineStage::FragmentShader;
  } else {
    ASSERT(0);
  }
  return sample_tex(id, stage);
}

RGResourceId RGPass::sample_tex(RGResourceId id, rhi::PipelineStage stage, int32_t subresource_mip,
                                int32_t subresource_slice) {
  add_read_usage(id, stage, rhi::AccessFlags::ShaderSampledRead, subresource_mip,
                 subresource_slice);
  return id;
}

RGResourceId RGPass::read_tex(RGResourceId id, rhi::PipelineStage stage, int32_t subresource_mip,
                              int32_t subresource_slice) {
  add_read_usage(id, stage, rhi::AccessFlags::ShaderStorageRead, subresource_mip,
                 subresource_slice);
  return id;
}

RGResourceId RGPass::write_tex(RGResourceId id, rhi::PipelineStage stage, int32_t subresource_mip,
                               int32_t subresource_slice) {
  rhi::AccessFlags access{};
  if (id.type == RGResourceType::ExternalTexture) {
    if (type_ == RGPassType::Transfer) {
      access = rhi::AccessFlags::TransferWrite;
    } else {
      access = rhi::AccessFlags::ShaderWrite;
    }
  } else {
    access = (type_ == RGPassType::Transfer) ? rhi::AccessFlags::TransferWrite
                                             : rhi::AccessFlags::ShaderWrite;
  }
  add_write_usage(id, stage, access, false, subresource_mip, subresource_slice);
  return id;
}

RGResourceId RGPass::write_color_output(RGResourceId id) {
  add_write_usage(id, rhi::PipelineStage::ColorAttachmentOutput,
                  rhi::AccessFlags::ColorAttachmentWrite);
  return id;
}

RGResourceId RGPass::write_depth_output(RGResourceId id) {
  add_write_usage(id,
                  rhi::PipelineStage::EarlyFragmentTests | rhi::PipelineStage::LateFragmentTests,
                  rhi::AccessFlags::DepthStencilWrite);
  return id;
}

RGResourceId RGPass::rw_color_output(RGResourceId input) {
  return rw_tex(input, rhi::PipelineStage::ColorAttachmentOutput,
                rhi::AccessFlags::ColorAttachmentRead,
                rhi::AccessFlags::ColorAttachmentRead | rhi::AccessFlags::ColorAttachmentWrite);
}

RGResourceId RGPass::rw_depth_output(RGResourceId input) {
  return rw_tex(input,
                rhi::PipelineStage::EarlyFragmentTests | rhi::PipelineStage::LateFragmentTests,
                rhi::AccessFlags::DepthStencilRead,
                rhi::AccessFlags::DepthStencilRead | rhi::AccessFlags::DepthStencilWrite);
}

RGResourceId RGPass::read_buf(RGResourceId id, rhi::PipelineStage stage) {
  const auto access = (id.type == RGResourceType::ExternalBuffer)
                          ? rhi::AccessFlags::ShaderRead
                          : rhi::AccessFlags::ShaderStorageRead;
  add_read_usage(id, stage, access);
  return id;
}

RGResourceId RGPass::copy_from_buf(RGResourceId id) {
  add_read_usage(id, rhi::PipelineStage::AllTransfer, rhi::AccessFlags::TransferRead);
  return id;
}

RGResourceId RGPass::read_buf(RGResourceId id, rhi::PipelineStage stage, rhi::AccessFlags access) {
  add_read_usage(id, stage, access);
  return id;
}

RGResourceId RGPass::write_buf(RGResourceId id, rhi::PipelineStage stage) {
  rhi::AccessFlags access{};
  if (type_ == RGPassType::Transfer) {
    access = rhi::AccessFlags::TransferWrite;
  } else {
    access = rhi::AccessFlags::ShaderStorageWrite;
  }
  // if (id.type == RGResourceType::ExternalBuffer) {
  //   access = (type_ == RGPassType::Transfer) ? rhi::AccessFlags::TransferWrite
  //                                            : rhi::AccessFlags::ShaderWrite;
  // } else {
  //   access = rhi::AccessFlags::ShaderStorageWrite;
  // }
  add_write_usage(id, stage, access);
  return id;
}

RGResourceId RGPass::rw_buf(RGResourceId input, rhi::PipelineStage stage) {
  const auto read_access = (input.type == RGResourceType::ExternalBuffer)
                               ? rhi::AccessFlags::ShaderRead
                               : rhi::AccessFlags::ShaderStorageRead;
  const auto write_access = (input.type == RGResourceType::ExternalBuffer)
                                ? ((type_ == RGPassType::Transfer) ? rhi::AccessFlags::TransferWrite
                                                                   : rhi::AccessFlags::ShaderWrite)
                                : rhi::AccessFlags::ShaderStorageWrite;
  auto output = rg_->next_version(input);
  add_read_usage(input, stage, read_access);
  add_write_usage(output, stage, read_access | write_access);
  return output;
}

RGResourceId RGPass::import_external_texture(rhi::TextureHandle tex_handle,
                                             std::string_view debug_name) {
  return rg_->import_external_texture(tex_handle, debug_name);
}

RGResourceId RGPass::import_external_texture(rhi::TextureHandle tex_handle, const RGState& initial,
                                             std::string_view debug_name) {
  return rg_->import_external_texture(tex_handle, initial, debug_name);
}

RGResourceId RGPass::import_external_buffer(rhi::BufferHandle buf_handle,
                                            std::string_view debug_name) {
  return rg_->import_external_buffer(buf_handle, debug_name);
}

RGResourceId RGPass::import_external_buffer(rhi::BufferHandle buf_handle, const RGState& initial,
                                            std::string_view debug_name) {
  return rg_->import_external_buffer(buf_handle, initial, debug_name);
}

void RenderGraph::shutdown() {
  auto destroy = [this](auto& resource_map) {
    for (auto& [key, handles] : resource_map) {
      (void)key;
      for (const auto& handle : handles) {
        device_->destroy(handle);
      }
    }
    resource_map.clear();
  };
  destroy(free_bufs_);
  destroy(free_atts_);
  destroy(defer_pool_pending_return_);
}

void RGPass::add_read_usage(RGResourceId id, rhi::PipelineStage stage, rhi::AccessFlags access,
                            int32_t subresource_mip, int32_t subresource_slice) {
  int32_t mip = subresource_mip;
  int32_t slice = subresource_slice;
  if (id.type == RGResourceType::Buffer || id.type == RGResourceType::ExternalBuffer) {
    mip = -1;
    slice = -1;
  }
  if (id.type == RGResourceType::ExternalTexture || id.type == RGResourceType::ExternalBuffer) {
    rg_->add_external_read_id(id);
    external_reads_.emplace_back(NameAndAccess{id, stage, access, id.type, false, mip, slice});
  } else {
    internal_reads_.emplace_back(NameAndAccess{id, stage, access, id.type, false, mip, slice});
  }
}

void RGPass::add_write_usage(RGResourceId id, rhi::PipelineStage stage, rhi::AccessFlags access,
                             bool is_swapchain_write, int32_t subresource_mip,
                             int32_t subresource_slice) {
  int32_t mip = subresource_mip;
  int32_t slice = subresource_slice;
  if (id.type == RGResourceType::Buffer || id.type == RGResourceType::ExternalBuffer) {
    mip = -1;
    slice = -1;
  }
  rg_->register_write(id, *this);
  if (id.type == RGResourceType::ExternalTexture || id.type == RGResourceType::ExternalBuffer) {
    external_writes_.emplace_back(
        NameAndAccess{id, stage, access, id.type, is_swapchain_write, mip, slice});
  } else {
    internal_writes_.emplace_back(
        NameAndAccess{id, stage, access, id.type, is_swapchain_write, mip, slice});
  }
}

RGResourceId RGPass::rw_tex(RGResourceId input, rhi::PipelineStage stage,
                            rhi::AccessFlags read_access, rhi::AccessFlags write_access,
                            int32_t read_subresource_mip, int32_t write_subresource_mip) {
  auto output = rg_->next_version(input);
  add_read_usage(input, stage, read_access, read_subresource_mip, -1);
  add_write_usage(output, stage, write_access, false, write_subresource_mip, -1);
  return output;
}

void RenderGraph::Pass::w_swapchain_tex(rhi::Swapchain* swapchain) {
  ASSERT(swapchain);
  swapchain_write_ = swapchain;
  auto curr_tex = swapchain->get_current_texture();
  ASSERT(type_ == RGPassType::Graphics);
  // TODO: remove
  auto swapchain_id = rg_->import_external_texture(
      curr_tex,
      RGState{.stage = rhi::PipelineStage::BottomOfPipe, .layout = rhi::ResourceLayout::Undefined},
      "swapchain");
  add_write_usage(swapchain_id, rhi::PipelineStage::ColorAttachmentOutput,
                  rhi::AccessFlags::ColorAttachmentWrite, true);
}
RenderGraph::ResourceRecord RenderGraph::create_resource_record(RGResourceType type,
                                                                uint32_t physical_idx,
                                                                std::string_view debug_name) {
  return {
      .type = type,
      .physical_idx = physical_idx,
      .debug_name = debug_name.empty() ? kInvalidNameId : intern_name(debug_name),
  };
}

void add_buffer_readback_copy(RenderGraph& rg, std::string_view pass_name, RGResourceId src_buf,
                              rhi::BufferHandle dst_buf, RGResourceId dst_rg_id, size_t src_offset,
                              size_t dst_offset, size_t size_bytes) {
  auto& p = rg.add_transfer_pass(pass_name);
  p.copy_from_buf(src_buf);
  p.write_buf(dst_rg_id, rhi::PipelineStage::AllTransfer);
  p.set_ex([&rg, src_buf, dst_buf, dst_offset, src_offset, size_bytes](rhi::CmdEncoder* enc) {
    enc->copy_buffer_to_buffer(rg.get_buf(src_buf), src_offset, dst_buf, dst_offset, size_bytes);
  });
}

}  // namespace gfx

}  // namespace TENG_NAMESPACE
