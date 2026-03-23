#include "RenderGraph.hpp"

#include <algorithm>
#include <ranges>
#include <string>
#include <tracy/Tracy.hpp>
#include <utility>

#include "core/Config.hpp"
#include "core/EAssert.hpp"
#include "core/Logger.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "gfx/rhi/Swapchain.hpp"
#include "gfx/rhi/Texture.hpp"
#include "small_vector/small_vector.hpp"

namespace TENG_NAMESPACE {

namespace gfx {

namespace {

const char* to_string(RGPassType type) {
  switch (type) {
    case RGPassType::Compute:
      return "Compute";
    case RGPassType::Graphics:
      return "Graphics";
    case RGPassType::Transfer:
      return "Transfer";
    case RGPassType::None:
      return "None";
  }
}
std::string rhi_pipeline_stage_to_string(rhi::PipelineStage stage) {
  std::string result;
  if (has_flag(stage, rhi::PipelineStage::TopOfPipe)) {
    result += "PipelineStage_TopOfPipe | ";
  }
  if (has_flag(stage, rhi::PipelineStage::DrawIndirect)) {
    result += "PipelineStage_DrawIndirect | ";
  }
  if (has_flag(stage, rhi::PipelineStage::VertexInput)) {
    result += "PipelineStage_VertexInput | ";
  }
  if (has_flag(stage, rhi::PipelineStage::VertexShader)) {
    result += "PipelineStage_VertexShader | ";
  }
  if (has_flag(stage, rhi::PipelineStage::TaskShader)) {
    result += "PipelineStage_TaskShader | ";
  }
  if (has_flag(stage, rhi::PipelineStage::MeshShader)) {
    result += "PipelineStage_MeshShader | ";
  }
  if (has_flag(stage, rhi::PipelineStage::FragmentShader)) {
    result += "PipelineStage_FragmentShader | ";
  }
  if (has_flag(stage, rhi::PipelineStage::EarlyFragmentTests)) {
    result += "PipelineStage_EarlyFragmentTests | ";
  }
  if (has_flag(stage, rhi::PipelineStage::LateFragmentTests)) {
    result += "PipelineStage_LateFragmentTests | ";
  }
  if (has_flag(stage, rhi::PipelineStage::ColorAttachmentOutput)) {
    result += "PipelineStage_ColorAttachmentOutput | ";
  }
  if (has_flag(stage, rhi::PipelineStage::ComputeShader)) {
    result += "PipelineStage_ComputeShader | ";
  }
  if (has_flag(stage, rhi::PipelineStage::AllTransfer)) {
    result += "PipelineStage_AllTransfer | ";
  }
  if (has_flag(stage, rhi::PipelineStage::BottomOfPipe)) {
    result += "PipelineStage_BottomOfPipe | ";
  }
  if (has_flag(stage, rhi::PipelineStage::Host)) {
    result += "PipelineStage_Host | ";
  }
  if (has_flag(stage, rhi::PipelineStage::AllGraphics)) {
    result += "PipelineStage_AllGraphics | ";
  }
  if (has_flag(stage, rhi::PipelineStage::AllCommands)) {
    result += "PipelineStage_AllCommands | ";
  }
  if (result.size()) {
    result = result.substr(0, result.size() - 3);
  }
  return result;
}

std::string rhi_access_to_string(rhi::AccessFlags access) {
  std::string result;
  if (has_flag(access, rhi::AccessFlags::IndirectCommandRead)) {
    result += "IndirectCommandRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::IndexRead)) {
    result += "IndexRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::VertexAttributeRead)) {
    result += "VertexAttributeRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::UniformRead)) {
    result += "UniformRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::InputAttachmentRead)) {
    result += "InputAttachmentRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::ShaderRead)) {
    result += "ShaderRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::ShaderWrite)) {
    result += "ShaderWrite | ";
  }
  if (has_flag(access, rhi::AccessFlags::ColorAttachmentRead)) {
    result += "ColorAttachmentRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::ColorAttachmentWrite)) {
    result += "ColorAttachmentWrite | ";
  }
  if (has_flag(access, rhi::AccessFlags::DepthStencilRead)) {
    result += "DepthStencilRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::DepthStencilWrite)) {
    result += "DepthStencilWrite | ";
  }
  if (has_flag(access, rhi::AccessFlags::TransferRead)) {
    result += "TransferRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::TransferWrite)) {
    result += "TransferWrite | ";
  }
  if (has_flag(access, rhi::AccessFlags::HostRead)) {
    result += "HostRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::HostWrite)) {
    result += "HostWrite | ";
  }
  if (has_flag(access, rhi::AccessFlags::MemoryRead)) {
    result += "MemoryRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::MemoryWrite)) {
    result += "MemoryWrite | ";
  }
  if (has_flag(access, rhi::AccessFlags::ShaderSampledRead)) {
    result += "ShaderSampledRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::ShaderStorageRead)) {
    result += "ShaderStorageRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::ShaderStorageWrite)) {
    result += "ShaderStorageWrite | ";
  }
  if (result.size()) {
    result = result.substr(0, result.size() - 3);
  }
  return result;
}

template <typename Flags, typename Bits>
constexpr Flags flag_or(Flags x, Bits y) noexcept {
  return static_cast<Flags>(static_cast<std::underlying_type_t<Flags>>(x) |
                            static_cast<uint64_t>(y));
}

[[maybe_unused]] rhi::ResourceState convert_resource_state(rhi::AccessFlags access) {
  if (has_flag(access, rhi::AccessFlags::ColorAttachmentWrite)) {
    return rhi::ResourceState::ColorWrite;
  }
  if (has_flag(access, rhi::AccessFlags::DepthStencilWrite)) {
    return rhi::ResourceState::DepthStencilWrite;
  }
  if (has_flag(access, rhi::AccessFlags::ShaderWrite)) {
    return rhi::ResourceState::ShaderWrite;
  }
  if (has_flag(access, rhi::AccessFlags::TransferWrite)) {
    return rhi::ResourceState::TransferWrite;
  }
  if (has_flag(access, rhi::AccessFlags::ShaderRead)) {
    return rhi::ResourceState::ShaderRead;
  }
  if (has_flag(access, rhi::AccessFlags::ColorAttachmentRead)) {
    return rhi::ResourceState::ColorRead;
  }
  if (has_flag(access, rhi::AccessFlags::DepthStencilRead)) {
    return rhi::ResourceState::DepthStencilRead;
  }
  if (has_flag(access, rhi::AccessFlags::IndirectCommandRead)) {
    return rhi::ResourceState::IndirectRead;
  }
  if (has_flag(access, rhi::AccessFlags::IndexRead)) {
    return rhi::ResourceState::IndexRead;
  }
  if (has_flag(access, rhi::AccessFlags::VertexAttributeRead)) {
    return rhi::ResourceState::VertexRead;
  }
  if (has_flag(access, rhi::AccessFlags::UniformRead)) {
    return rhi::ResourceState::ShaderRead;
  }
  if (has_flag(access, rhi::AccessFlags::InputAttachmentRead)) {
    ASSERT(0);
  }
  ASSERT(0);
  return rhi::ResourceState::None;
}

}  // namespace

RenderGraph::Pass::Pass(std::string name, RenderGraph* rg, uint32_t pass_i, RGPassType type)
    : rg_(rg), pass_i_(pass_i), name_(std::move(name)), type_(type) {}

RenderGraph::Pass& RenderGraph::add_pass(const std::string& name, RGPassType type) {
  auto idx = static_cast<uint32_t>(passes_.size());
  passes_.emplace_back(name, this, idx, type);
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
        enc->barrier(buf_handle, barrier.src_stage, barrier.src_access, barrier.dst_stage,
                     barrier.dst_access);
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
        enc->barrier(tex_handle, barrier.src_stage, barrier.src_access, barrier.dst_stage,
                     barrier.dst_access);
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
    free_atts_[tex_att_infos_[i]].emplace_back(tex_att_handles_[i]);
  }
  // history bufs are now gtg
  for (auto& [info, bufs] : history_free_bufs_) {
    for (auto& buf : bufs) {
      free_bufs_[info].emplace_back(buf);
    }
  }
  history_free_bufs_.clear();

  for (size_t i = 0; i < buffer_infos_.size(); i++) {
    if (history_buffer_handles_[i].is_valid()) {
      history_free_bufs_[buffer_infos_[i]].emplace_back(history_buffer_handles_[i]);
    } else {
      free_bufs_[buffer_infos_[i]].emplace_back(buffer_handles_[i]);
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
  curr_submitted_swapchain_textures_.clear();
}

void RenderGraph::bake(glm::uvec2 fb_size, bool verbose) {
  ZoneScoped;
  if (verbose) {
    LINFO("//////////// Baking Render Graph ////////////");
  }
  {
    sink_passes_.clear();
    pass_dependencies_.clear();
    intermed_pass_visited_.clear();
    intermed_pass_stack_.clear();
    pass_stack_.clear();

    static std::vector<AttachmentInfo> stale_atts;
    stale_atts.clear();

    for (auto& [att_info, handles] : free_atts_) {
      ASSERT(!handles.empty());
      auto* tex = device_->get_tex(handles[0]);
      if (att_info.size_class == SizeClass::Swapchain && glm::uvec2{tex->desc().dims} != fb_size) {
        for (const auto& handle : handles) {
          device_->destroy(handle);
        }
        stale_atts.emplace_back(att_info);
      }
    }
    for (auto& stale_att : stale_atts) {
      free_atts_.erase(stale_att);
    }
  }

  // find sink nodes, ie nodes that don't write to anything
  sink_passes_.clear();
  ALWAYS_ASSERT(passes_.size() > 0);
  // find resource that don't get read (swapchain img)
  // find pass that writes to that resource
  for (size_t pass_i = 0; pass_i < passes_.size(); pass_i++) {
    auto& pass = passes_[pass_i];
    bool sink = false;
    if (pass.swapchain_write_) {
      sink = true;
    } else {
      for (const auto& write : pass.get_external_writes()) {
        if (!external_read_ids_.contains(write.id)) {
          sink = true;
          break;
        }
      }
    }
    if (sink) {
      sink_passes_.push_back(pass_i);
    }
  }

  if (verbose) {
    LINFO("Sink passes:");
    for (auto p : sink_passes_) {
      LINFO("\t{}", passes_[p].get_name());
    }
    LINFO("\n\n");
  }

  {  // pass ordering
    for (auto& p : pass_dependencies_) {
      p.clear();
    }
    pass_dependencies_.resize(passes_.size());
    for (uint32_t pass_i : sink_passes_) {
      intermed_pass_stack_.push_back(pass_i);
      find_deps_recursive(pass_i, 0);
    }
    static std::unordered_set<uint32_t> curr_stack_passes;
    static std::unordered_set<uint32_t> visited_passes;
    curr_stack_passes.clear();
    visited_passes.clear();
    curr_stack_passes.reserve(passes_.size());
    visited_passes.reserve(passes_.size());
    pass_stack_.clear();
    for (uint32_t root : intermed_pass_stack_) {
      dfs(pass_dependencies_, curr_stack_passes, visited_passes, pass_stack_, root);
    }
    if (verbose) {
      LINFO("//////////////// Pass Order ////////////////");
      for (auto pass_i : pass_stack_) {
        auto& pass = passes_[pass_i];
        LINFO("[PASS]: {}", pass.get_name());
        const std::vector<RenderGraph::Pass::NameAndAccess>* arrays[4] = {
            &pass.get_internal_reads(), &pass.get_internal_writes(), &pass.get_external_reads(),
            &pass.get_external_writes()};
        for (auto& arr : arrays) {
          for (const auto& u : *arr) {
            LINFO("{:<50}\t{:<50}\t{:<50}", debug_name(u.id), rhi_access_to_string(u.acc),
                  rhi_pipeline_stage_to_string(u.stage));
          }
        }
      }
      LINFO("");
    }
  }

  {
    // create attachment images
    tex_att_handles_.clear();
    for (const auto& att_info : tex_att_infos_) {
      if (att_info.is_swapchain_tex) {
        ASSERT(0);
        continue;
      }
      auto get_att_dims = [&att_info, &fb_size]() {
        if (att_info.size_class == SizeClass::Swapchain) {
          return fb_size;
        }
        return att_info.dims;
      };

      rhi::TextureHandle actual_att_handle{};
      auto free_att_it = free_atts_.find(att_info);
      if (free_att_it != free_atts_.end()) {
        auto& texture_handles = free_att_it->second;
        if (!texture_handles.empty()) {
          actual_att_handle = texture_handles.back();
          texture_handles.pop_back();
        }
      }

      if (!actual_att_handle.is_valid()) {
        auto dims = get_att_dims();
        auto att_tx_handle = device_->create_tex(rhi::TextureDesc{
            .format = att_info.format,
            .usage =
                (!is_depth_format(att_info.format) ? rhi::TextureUsage::ColorAttachment
                                                   : rhi::TextureUsage::DepthStencilAttachment) |
                // TODO: track usages
                rhi::TextureUsage::Sample | rhi::TextureUsage::Storage,
            .dims = glm::uvec3{dims.x, dims.y, 1},
            .mip_levels = att_info.mip_levels,
            .array_length = att_info.array_layers,
            .name = "render_graph_tex_att"});
        actual_att_handle = att_tx_handle;
      }

      tex_att_handles_.emplace_back(actual_att_handle);
    }
    ASSERT(tex_att_handles_.size() == tex_att_infos_.size());
  }
  {  // create buffers
    buffer_handles_.clear();
    history_buffer_handles_.clear();
    for (const auto& binfo : buffer_infos_) {
      rhi::BufferHandle actual_buf_handle{};
      auto free_buf_it = free_bufs_.find(binfo);
      if (free_buf_it != free_bufs_.end()) {
        auto& buffer_handles = free_buf_it->second;
        if (!buffer_handles.empty()) {
          actual_buf_handle = buffer_handles.back();
          buffer_handles.pop_back();
          if (buffer_handles.empty()) {
            free_bufs_.erase(free_buf_it);
          }
        }
      }
      if (!actual_buf_handle.is_valid()) {
        auto buf_handle = device_->create_buf(rhi::BufferDesc{
            .usage = (rhi::BufferUsage)(rhi::BufferUsage::Storage),
            .size = binfo.size,
            .name = "render_graph_buffer",
        });
        actual_buf_handle = buf_handle;
      }
      buffer_handles_.emplace_back(actual_buf_handle);
      if (binfo.defer_reuse) {
        history_buffer_handles_.emplace_back(actual_buf_handle);
      } else {
        history_buffer_handles_.emplace_back();
      }
    }
  }
  // enqueue delete unused free buffers
  for (auto& [binfo, handles] : free_bufs_) {
    for (const auto& handle : handles) {
      device_->destroy(handle);
    }
  }
  free_bufs_.clear();

  struct ResourceState {
    rhi::AccessFlags access;
    rhi::PipelineStage stage;
  };

  static std::vector<ResourceState> states[4];
  for (auto& state : states) state.clear();
  states[(int)RGResourceType::Texture].resize(tex_att_infos_.size());
  states[(int)RGResourceType::Buffer].resize(buffer_infos_.size());
  states[(int)RGResourceType::ExternalTexture].resize(external_textures_.size());
  states[(int)RGResourceType::ExternalBuffer].resize(external_buffers_.size());

  auto get_resource_state = [](RGResourceHandle handle) -> ResourceState& {
    return states[(int)handle.type][handle.idx];
  };

  for (auto& b : pass_barrier_infos_) {
    b.clear();
  }
  if (pass_barrier_infos_.size() < passes_.size()) {
    pass_barrier_infos_.resize(passes_.size());
  }
  if (verbose) {
    LINFO("//////////////// Barriers ////////////////");
  }
  for (auto pass_i : pass_stack_) {
    ASSERT(pass_i < passes_.size());
    const auto& pass = passes_[pass_i];
    auto& barriers = pass_barrier_infos_[pass_i];

    for (const auto& write_use : pass.get_external_writes()) {
      auto rg_resource_handle = get_physical_handle(write_use.id);
      auto& resource_state = get_resource_state(rg_resource_handle);
      if (has_flag(write_use.acc, rhi::AccessFlags::AnyWrite)) {
        barriers.emplace_back(BarrierInfo{
            .resource = rg_resource_handle,
            .src_stage = resource_state.stage,
            .dst_stage = write_use.stage,
            .src_access = resource_state.access,
            .dst_access = write_use.acc,
            .debug_id = write_use.id,
            .is_swapchain_write = write_use.is_swapchain_write,
        });
      }
      resource_state.access = flag_or(resource_state.access, write_use.acc);
      resource_state.stage = flag_or(resource_state.stage, write_use.stage);
    }

    for (const auto& write_use : pass.get_internal_writes()) {
      auto rg_resource_handle = get_physical_handle(write_use.id);
      auto& resource_state = get_resource_state(rg_resource_handle);
      if (has_flag(write_use.acc, rhi::AccessFlags::AnyWrite)) {
        barriers.emplace_back(BarrierInfo{
            .resource = rg_resource_handle,
            .src_stage = resource_state.stage,
            .dst_stage = write_use.stage,
            .src_access = resource_state.access,
            .dst_access = write_use.acc,
            .debug_id = write_use.id,
        });
      }
      resource_state.access = flag_or(resource_state.access, write_use.acc);
      resource_state.stage = flag_or(resource_state.stage, write_use.stage);
    }

    for (const auto& read_use : pass.get_external_reads()) {
      auto rg_resource_handle = get_physical_handle(read_use.id);
      auto& resource_state = get_resource_state(rg_resource_handle);
      barriers.emplace_back(BarrierInfo{
          .resource = rg_resource_handle,
          .src_stage = resource_state.stage,
          .dst_stage = read_use.stage,
          .src_access = resource_state.access,
          .dst_access = read_use.acc,
          .debug_id = read_use.id,
      });
      if (has_flag(read_use.acc, rhi::AccessFlags::AnyWrite)) {
        resource_state.access = flag_or(resource_state.access, read_use.acc);
        resource_state.stage = flag_or(resource_state.stage, read_use.stage);
      }
    }
    for (const auto& read_use : pass.get_internal_reads()) {
      auto rg_resource_handle = get_physical_handle(read_use.id);
      const auto& state = get_resource_state(rg_resource_handle);
      barriers.emplace_back(BarrierInfo{
          .resource = rg_resource_handle,
          .src_stage = state.stage,
          .dst_stage = read_use.stage,
          .src_access = state.access,
          .dst_access = read_use.acc,
          .debug_id = read_use.id,
      });
    }

#define CLR_CYAN "\033[36m"
#define CLR_PURPLE "\033[35m"
#define CLR_GREEN "\033[32m"
#define CLR_RESET "\033[0m"

    if (verbose) {
      LINFO(CLR_GREEN "{} Pass" CLR_RESET ": {}", to_string(pass.type()), pass.get_name());
      for (auto& barrier : barriers) {
        const auto dbg_name = debug_name(barrier.debug_id);
        LINFO(CLR_PURPLE "RESOURCE: {}" CLR_RESET "", dbg_name.size() ? dbg_name : "no name lol");
        LINFO("\t{} {}", std::format(CLR_CYAN "{:<14}" CLR_RESET, "SRC_ACCESS:"),
              rhi_access_to_string(barrier.src_access));
        LINFO("\t{} {}", std::format(CLR_CYAN "{:<14}" CLR_RESET, "DST_ACCESS:"),
              rhi_access_to_string(barrier.dst_access));
        LINFO("\t{} {}", std::format(CLR_CYAN "{:<14}" CLR_RESET, "SRC_STAGE:"),
              rhi_pipeline_stage_to_string(barrier.src_stage));
        LINFO("\t{} {}", std::format(CLR_CYAN "{:<14}" CLR_RESET, "DST_STAGE:"),
              rhi_pipeline_stage_to_string(barrier.dst_stage));
      }
      LINFO("");
    }
  }

  if (verbose) {
    LINFO("//////////// Done Baking Render Graph ////////////");
  }
}

RenderGraph::NameId RenderGraph::intern_name(const std::string& name) {
  auto it = name_to_id_.find(name);
  if (it != name_to_id_.end()) {
    return it->second;
  }
  const auto id = static_cast<NameId>(id_to_name_.size());
  id_to_name_.push_back(name);
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
  const auto key = tex_handle.to64();
  if (auto it = external_tex_handle_to_id_.find(key); it != external_tex_handle_to_id_.end()) {
    if (!debug_name.empty()) {
      auto& rec = resources_[it->second.idx];
      if (rec.debug_name == kInvalidNameId) {
        rec.debug_name = intern_name(std::string(debug_name));
      }
    }
    const auto& rec = resources_[it->second.idx];
    return RGResourceId{.idx = it->second.idx,
                        .type = RGResourceType::ExternalTexture,
                        .version = rec.latest_version};
  }
  auto idx = external_textures_.size();
  external_textures_.emplace_back(tex_handle);
  auto record = create_resource_record(RGResourceType::ExternalTexture, idx, debug_name);
  resources_.push_back(record);
  RGResourceId id{.idx = static_cast<uint32_t>(resources_.size() - 1),
                  .type = RGResourceType::ExternalTexture,
                  .version = 0};
  external_tex_handle_to_id_.emplace(key, id);
  return id;
}

RGResourceId RenderGraph::import_external_buffer(rhi::BufferHandle buf_handle,
                                                 std::string_view debug_name) {
  const auto key = buf_handle.to64();
  if (auto it = external_buf_handle_to_id_.find(key); it != external_buf_handle_to_id_.end()) {
    if (!debug_name.empty()) {
      auto& rec = resources_[it->second.idx];
      if (rec.debug_name == kInvalidNameId) {
        rec.debug_name = intern_name(std::string(debug_name));
      }
    }
    const auto& rec = resources_[it->second.idx];
    return RGResourceId{.idx = it->second.idx,
                        .type = RGResourceType::ExternalBuffer,
                        .version = rec.latest_version};
  }
  auto idx = external_buffers_.size();
  external_buffers_.emplace_back(buf_handle);
  auto record = create_resource_record(RGResourceType::ExternalBuffer, idx, debug_name);
  resources_.push_back(record);
  RGResourceId id{.idx = static_cast<uint32_t>(resources_.size() - 1),
                  .type = RGResourceType::ExternalBuffer,
                  .version = 0};
  external_buf_handle_to_id_.emplace(key, id);
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

void RenderGraph::find_deps_recursive(uint32_t pass_i, uint32_t stack_size) {
  // TODO: rid of stack size
  if (stack_size > passes_.size() * 100) {
    ALWAYS_ASSERT(0 && "RenderGraph: Cycle");
  }
  if (intermed_pass_visited_.contains(pass_i)) {
    return;
  }
  intermed_pass_visited_.insert(pass_i);
  auto& pass = passes_[pass_i];
  stack_size++;

  for (const auto& external_read : pass.get_external_reads()) {
    auto writer_pass_it = resource_use_id_to_writer_pass_idx_.find(external_read.id);
    ALWAYS_ASSERT(writer_pass_it != resource_use_id_to_writer_pass_idx_.end());
    uint32_t write_pass_i = writer_pass_it->second;
    intermed_pass_stack_.emplace_back(write_pass_i);
    pass_dependencies_[pass_i].insert(write_pass_i);
    find_deps_recursive(write_pass_i, stack_size);
  }

  for (const auto& read : pass.get_internal_reads()) {
    auto writer_pass_it = resource_use_id_to_writer_pass_idx_.find(read.id);
    if (writer_pass_it == resource_use_id_to_writer_pass_idx_.end()) {
      LERROR("not found: {}", debug_name(read.id));
      ASSERT(0);
    }
    uint32_t write_pass_i = writer_pass_it->second;
    intermed_pass_stack_.emplace_back(write_pass_i);
    pass_dependencies_[pass_i].insert(write_pass_i);
    find_deps_recursive(write_pass_i, stack_size);
  }
}

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

RGResourceId RGPass::sample_tex(RGResourceId id, rhi::PipelineStage stage) {
  add_read_usage(id, stage, rhi::AccessFlags::ShaderSampledRead);
  return id;
}

RGResourceId RGPass::read_tex(RGResourceId id, rhi::PipelineStage stage) {
  add_read_usage(id, stage, rhi::AccessFlags::ShaderStorageRead);
  return id;
}

RGResourceId RGPass::write_tex(RGResourceId id, rhi::PipelineStage stage) {
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
  add_write_usage(id, stage, access);
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
                rhi::AccessFlags::ColorAttachmentRead | rhi::AccessFlags::ColorAttachmentWrite);
}

RGResourceId RGPass::rw_depth_output(RGResourceId input) {
  return rw_tex(input,
                rhi::PipelineStage::EarlyFragmentTests | rhi::PipelineStage::LateFragmentTests,
                rhi::AccessFlags::DepthStencilRead | rhi::AccessFlags::DepthStencilWrite);
}

RGResourceId RGPass::read_buf(RGResourceId id, rhi::PipelineStage stage) {
  const auto access = (id.type == RGResourceType::ExternalBuffer)
                          ? rhi::AccessFlags::ShaderRead
                          : rhi::AccessFlags::ShaderStorageRead;
  add_read_usage(id, stage, access);
  return id;
}

RGResourceId RGPass::write_buf(RGResourceId id, rhi::PipelineStage stage) {
  rhi::AccessFlags access{};
  if (id.type == RGResourceType::ExternalBuffer) {
    access = (type_ == RGPassType::Transfer) ? rhi::AccessFlags::TransferWrite
                                             : rhi::AccessFlags::ShaderWrite;
  } else {
    access = rhi::AccessFlags::ShaderStorageWrite;
  }
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

RGResourceId RGPass::import_external_buffer(rhi::BufferHandle buf_handle,
                                            std::string_view debug_name) {
  return rg_->import_external_buffer(buf_handle, debug_name);
}

void RenderGraph::dfs(const std::vector<std::unordered_set<uint32_t>>& pass_dependencies,
                      std::unordered_set<uint32_t>& curr_stack_passes,
                      std::unordered_set<uint32_t>& visited_passes,
                      std::vector<uint32_t>& pass_stack, uint32_t pass) {
  if (curr_stack_passes.contains(pass)) {
    ASSERT(0 && "Cycle detected");
  }
  if (visited_passes.contains(pass)) return;

  curr_stack_passes.insert(pass);

  for (const auto& dep : pass_dependencies[pass]) {
    dfs(pass_dependencies, curr_stack_passes, visited_passes, pass_stack, dep);
  }

  curr_stack_passes.erase(pass);
  visited_passes.insert(pass);
  pass_stack.push_back(pass);
}

void RenderGraph::shutdown() {
  auto destroy = [this](auto& resource_map) {
    for (auto& [att_info, handles] : resource_map) {
      for (const auto& handle : handles) {
        device_->destroy(handle);
      }
    }
    resource_map.clear();
  };
  destroy(free_bufs_);
  destroy(free_atts_);
  destroy(history_free_bufs_);
}

void RGPass::add_read_usage(RGResourceId id, rhi::PipelineStage stage, rhi::AccessFlags access) {
  if (id.type == RGResourceType::ExternalTexture || id.type == RGResourceType::ExternalBuffer) {
    rg_->add_external_read_id(id);
    external_reads_.emplace_back(NameAndAccess{id, stage, access, id.type});
  } else {
    internal_reads_.emplace_back(NameAndAccess{id, stage, access, id.type});
  }
}

void RGPass::add_write_usage(RGResourceId id, rhi::PipelineStage stage, rhi::AccessFlags access,
                             bool is_swapchain_write) {
  rg_->register_write(id, *this);
  if (id.type == RGResourceType::ExternalTexture || id.type == RGResourceType::ExternalBuffer) {
    external_writes_.emplace_back(NameAndAccess{id, stage, access, id.type, is_swapchain_write});
  } else {
    internal_writes_.emplace_back(NameAndAccess{id, stage, access, id.type, is_swapchain_write});
  }
}

RGResourceId RGPass::rw_tex(RGResourceId input, rhi::PipelineStage stage, rhi::AccessFlags access) {
  auto output = rg_->next_version(input);
  add_read_usage(input, stage, access);
  add_write_usage(output, stage, access);
  return output;
}

void RenderGraph::Pass::w_swapchain_tex(rhi::Swapchain* swapchain) {
  ASSERT(swapchain);
  swapchain_write_ = swapchain;
  auto curr_tex = swapchain->get_current_texture();
  ASSERT(type_ == RGPassType::Graphics);
  auto swapchain_id = rg_->import_external_texture(curr_tex, "swapchain");
  add_write_usage(swapchain_id, rhi::PipelineStage::ColorAttachmentOutput,
                  rhi::AccessFlags::ColorAttachmentWrite, true);
}
RenderGraph::ResourceRecord RenderGraph::create_resource_record(RGResourceType type,
                                                                uint32_t physical_idx,
                                                                std::string_view debug_name) {
  return {
      .type = type,
      .physical_idx = physical_idx,
      .debug_name = debug_name.empty() ? kInvalidNameId : intern_name(std::string(debug_name)),
  };
}

void add_buffer_readback_copy(RenderGraph& rg, std::string_view pass_name, RGResourceId src_buf,
                              rhi::BufferHandle dst_buf, RGResourceId dst_rg_id, size_t src_offset,
                              size_t dst_offset, size_t size_bytes) {
  auto& p = rg.add_transfer_pass(std::string(pass_name));
  p.read_buf(src_buf, rhi::PipelineStage::AllTransfer);
  p.write_buf(dst_rg_id, rhi::PipelineStage::AllTransfer);
  p.set_ex([&rg, src_buf, dst_buf, dst_offset, src_offset, size_bytes](rhi::CmdEncoder* enc) {
    enc->copy_buffer_to_buffer(rg.get_buf(src_buf), src_offset, dst_buf, dst_offset, size_bytes);
  });
}

}  // namespace gfx

}  // namespace TENG_NAMESPACE
