#include "RenderGraph.hpp"

#include <ranges>
#include <tracy/Tracy.hpp>
#include <utility>

#include "core/Config.hpp"
#include "core/EAssert.hpp"
#include "core/Logger.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/Texture.hpp"

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

bool is_access_write(rhi::AccessFlags access) {
  return has_flag(access, rhi::AccessFlags::AnyWrite);
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

void convert_rg_access(RGAccess access, rhi::AccessFlags& out_access,
                       rhi::PipelineStage& out_stages) {
  if (access & ColorWrite) {
    out_access = flag_or(out_access, rhi::AccessFlags::ColorAttachmentWrite);
    out_stages = flag_or(out_stages, rhi::PipelineStage::ColorAttachmentOutput);
  }
  if (access & ShaderRead) {
    out_access = flag_or(out_access, rhi::AccessFlags::ShaderRead);
    out_stages = flag_or(out_stages, rhi::PipelineStage::AllGraphics);
  }
  if (access & ColorRead) {
    out_access = flag_or(out_access, rhi::AccessFlags::ColorAttachmentRead);
    out_stages = flag_or(out_stages, rhi::PipelineStage::ColorAttachmentOutput);
  }
  if (access & DepthStencilRead) {
    out_access = flag_or(out_access, rhi::AccessFlags::DepthStencilRead);
    out_stages = flag_or(
        out_stages, rhi::PipelineStage::EarlyFragmentTests | rhi::PipelineStage::LateFragmentTests);
  }
  if (access & DepthStencilWrite) {
    out_access = flag_or(out_access, rhi::AccessFlags::DepthStencilWrite);
    out_stages = flag_or(
        out_stages, rhi::PipelineStage::EarlyFragmentTests | rhi::PipelineStage::LateFragmentTests);
  }
  if (access & VertexRead) {
    out_access = flag_or(out_access, rhi::AccessFlags::ShaderStorageRead);
    out_stages = flag_or(out_stages, rhi::PipelineStage::VertexShader);
  }
  if (access & IndexRead) {
    out_access = flag_or(out_access, rhi::AccessFlags::ShaderStorageRead);
    out_stages = flag_or(out_stages, rhi::PipelineStage::VertexShader);
  }
  if (access & IndirectRead) {
    out_access = flag_or(out_access, rhi::AccessFlags::IndirectCommandRead);
    out_stages = flag_or(out_stages, rhi::PipelineStage::DrawIndirect);
  }
  if (access & ComputeRead) {
    out_access = flag_or(out_access, rhi::AccessFlags::ShaderRead);
    out_stages = flag_or(out_stages, rhi::PipelineStage::ComputeShader);
  }
  if (access & ComputeWrite) {
    out_access = flag_or(out_access, rhi::AccessFlags::ShaderWrite);
    out_stages = flag_or(out_stages, rhi::PipelineStage::ComputeShader);
  }
  if (access & ComputeSample) {
    out_access = flag_or(out_access, rhi::AccessFlags::ShaderSampledRead);
    out_stages = flag_or(out_stages, rhi::PipelineStage::ComputeShader);
  }
  if (access & FragmentSample) {
    out_access = flag_or(out_access, rhi::AccessFlags::ShaderSampledRead);
    out_stages = flag_or(out_stages, rhi::PipelineStage::FragmentShader);
  }
  if (access & FragmentStorageRead) {
    out_access = flag_or(out_access, rhi::AccessFlags::ShaderStorageRead);
    out_stages = flag_or(out_stages, rhi::PipelineStage::FragmentShader);
  }
  if (access & TransferWrite) {
    out_access = flag_or(out_access, rhi::AccessFlags::TransferWrite);
    out_stages = flag_or(out_stages, rhi::PipelineStage::AllTransfer);
  }
  if (access & TransferRead) {
    out_access = flag_or(out_access, rhi::AccessFlags::TransferRead);
    out_stages = flag_or(out_stages, rhi::PipelineStage::AllTransfer);
  }
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
  ZoneScoped;
  rhi::CmdEncoder* enc = device_->begin_cmd_encoder();
  for (auto pass_i : pass_stack_) {
    // enc->set_label(passes_[pass_i].get_name());
    for (auto& barrier : pass_barrier_infos_[pass_i]) {
      enc->barrier(barrier.src_stage, barrier.src_access, barrier.dst_stage, barrier.dst_access);
      if (barrier.resource.type == RGResourceType::Buffer ||
          barrier.resource.type == RGResourceType::ExternalBuffer) {
        auto buf_handle = barrier.resource.type == RGResourceType::Buffer
                              ? get_buf(barrier.resource)
                              : get_external_buf(barrier.resource);
        ASSERT(buf_handle.is_valid());
        enc->barrier(buf_handle, barrier.src_stage, barrier.src_access, barrier.dst_stage,
                     barrier.dst_access);
      } else {
        enc->barrier(barrier.src_stage, barrier.src_access, barrier.dst_stage, barrier.dst_access);
      }
    }

    auto& pass = passes_[pass_i];
    enc->set_debug_name(pass.get_name().c_str());
    pass.get_execute_fn()(enc);
  }
  enc->end_encoding();

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
  resource_name_to_handle_.clear();
  tex_handle_to_handle_.clear();
  resource_read_name_to_writer_pass_.clear();
  external_name_to_handle_idx_.clear();
  resource_use_name_to_writer_pass_idx_.clear();
  external_buffers_.clear();
  external_textures_.clear();
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
        if (!external_read_names_.contains(write.name)) {
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
            LINFO("{:<50}\t{:<50}\t{:<50}", u.name, rhi_access_to_string(u.acc),
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
            .usage = is_depth_format(att_info.format) ? rhi::TextureUsage::ColorAttachment
                                                      : rhi::TextureUsage::DepthStencilAttachment,
            .dims = glm::uvec3{dims.x, dims.y, 1},
            .mip_levels = att_info.mip_levels,
            .array_length = att_info.array_layers,
            .bindless = true,
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
            .storage_mode = rhi::StorageMode::GPUOnly,
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
      auto rg_resource_handle = RGResourceHandle{
          .idx = external_name_to_handle_idx_.at(write_use.name), .type = write_use.type};
      // if (write_use.name == "out_counts_buf3") {
      //   LINFO("write {} {} state stage: {}, state access: {}", pass.get_name(), write_use.name,
      //         rhi_pipeline_stage_to_string(write_use.stage),
      //         rhi_access_to_string(write_use.acc));
      // }
      auto& resource_state = get_resource_state(rg_resource_handle);
      resource_state.access = flag_or(resource_state.access, write_use.acc);
      resource_state.stage = flag_or(resource_state.stage, write_use.stage);
    }

    for (const auto& write_use : pass.get_internal_writes()) {
      auto rg_resource_handle = resource_name_to_handle_.at(write_use.name);
      auto& resource_state = get_resource_state(rg_resource_handle);
      if (has_flag(write_use.acc, rhi::AccessFlags::AnyWrite)) {
        barriers.emplace_back(BarrierInfo{
            .resource = rg_resource_handle,
            .src_stage = resource_state.stage,
            .dst_stage = write_use.stage,
            .src_access = resource_state.access,
            .dst_access = write_use.acc,
            .debug_name = write_use.name,
        });
      }
      resource_state.access = flag_or(resource_state.access, write_use.acc);
      resource_state.stage = flag_or(resource_state.stage, write_use.stage);
    }

    for (const auto& read_use : pass.get_external_reads()) {
      auto rg_resource_handle = RGResourceHandle{
          .idx = external_name_to_handle_idx_.at(read_use.name), .type = read_use.type};
      auto& resource_state = get_resource_state(rg_resource_handle);
      barriers.emplace_back(BarrierInfo{
          .resource = rg_resource_handle,
          .src_stage = resource_state.stage,
          .dst_stage = read_use.stage,
          .src_access = resource_state.access,
          .dst_access = read_use.acc,
          .debug_name = read_use.name,
      });
      if (has_flag(read_use.acc, rhi::AccessFlags::AnyWrite)) {
        resource_state.access = flag_or(resource_state.access, read_use.acc);
        resource_state.stage = flag_or(resource_state.stage, read_use.stage);
      }
    }
    for (const auto& read_use : pass.get_internal_reads()) {
      auto rg_resource_handle = resource_name_to_handle_.at(read_use.name);
      const auto& state = get_resource_state(rg_resource_handle);
      barriers.emplace_back(BarrierInfo{
          .resource = rg_resource_handle,
          .src_stage = state.stage,
          .dst_stage = read_use.stage,
          .src_access = state.access,
          .dst_access = read_use.acc,
          .debug_name = read_use.name,
      });
    }

#define CLR_CYAN "\033[36m"
#define CLR_PURPLE "\033[35m"
#define CLR_GREEN "\033[32m"
#define CLR_RESET "\033[0m"

    if (verbose) {
      LINFO(CLR_GREEN "{} Pass" CLR_RESET ": {}", to_string(pass.type()), pass.get_name());
      for (auto& barrier : barriers) {
        LINFO(CLR_PURPLE "RESOURCE: {}" CLR_RESET "",
              barrier.debug_name.size() ? barrier.debug_name : "no name lol");
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

void RenderGraph::init(rhi::Device* device) {
  device_ = device;
  passes_.reserve(200);
}

RGResourceHandle RenderGraph::add_tex_usage(const std::string& name, const AttachmentInfo& att_info,
                                            RGAccess, RGPass& pass) {
  ASSERT(!resource_name_to_handle_.contains(name));
  ASSERT((att_info.is_swapchain_tex || att_info.format != rhi::TextureFormat::Undefined));
  resource_use_name_to_writer_pass_idx_.emplace(name, pass.get_idx());

  RGResourceHandle handle = {.idx = static_cast<uint32_t>(tex_att_infos_.size()),
                             .type = RGResourceType::Texture};
  tex_att_infos_.emplace_back(att_info);
  resource_name_to_handle_.emplace(name, handle);
  return handle;
}

RGResourceHandle RenderGraph::add_buf_usage(const std::string& name, const BufferInfo& buf_info,
                                            Pass& pass) {
  ASSERT(!resource_name_to_handle_.contains(name));
  resource_use_name_to_writer_pass_idx_.emplace(name, pass.get_idx());

  RGResourceHandle handle = {.idx = static_cast<uint32_t>(buffer_infos_.size()),
                             .type = RGResourceType::Buffer};
  buffer_infos_.emplace_back(buf_info);
  resource_name_to_handle_.emplace(name, handle);
  return handle;
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
    const auto& read_name = is_access_write(external_read.acc)
                                ? pass.get_resource_read_names(external_read)
                                : external_read.name;
    auto writer_pass_it = resource_use_name_to_writer_pass_idx_.find(read_name);
    ALWAYS_ASSERT(writer_pass_it != resource_use_name_to_writer_pass_idx_.end());
    uint32_t write_pass_i = writer_pass_it->second;
    intermed_pass_stack_.emplace_back(write_pass_i);
    pass_dependencies_[pass_i].insert(write_pass_i);
    find_deps_recursive(write_pass_i, stack_size);
  }

  for (const auto& read : pass.get_internal_reads()) {
    const auto& read_name =
        is_access_write(read.acc) ? pass.get_resource_read_names(read) : read.name;
    auto writer_pass_it = resource_use_name_to_writer_pass_idx_.find(read_name);
    ALWAYS_ASSERT(writer_pass_it != resource_use_name_to_writer_pass_idx_.end());
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

rhi::TextureHandle RenderGraph::get_att_img(RGResourceHandle tex_handle) {
  ASSERT(tex_handle.idx < tex_att_handles_.size());
  ASSERT(tex_handle.type == RGResourceType::Texture);
  return tex_att_handles_[tex_handle.idx];
}

rhi::BufferHandle RenderGraph::get_buf(RGResourceHandle buf_handle) {
  ASSERT(buf_handle.idx < buffer_handles_.size());
  ASSERT(buf_handle.type == RGResourceType::Buffer);
  return buffer_handles_[buf_handle.idx];
}

RGResourceHandle RGPass::sample_tex(const std::string& name) {
  ASSERT(!name.empty());
  rhi::PipelineStage stage{};
  if (type_ == RGPassType::Compute) {
    stage = rhi::PipelineStage::ComputeShader;
  } else if (type_ == RGPassType::Graphics) {
    stage = rhi::PipelineStage::FragmentShader;
  } else {
    ASSERT(0);
  }
  internal_reads_.emplace_back(
      NameAndAccess{name, stage, rhi::AccessFlags::ShaderSampledRead, RGResourceType::Texture});
  return rg_->get_resource(name, RGResourceType::Texture);
}

RGResourceHandle RGPass::r_tex(const std::string& name) {
  rhi::PipelineStage stage{};
  if (type_ == RGPassType::Compute) {
    stage = rhi::PipelineStage::ComputeShader;
  } else if (type_ == RGPassType::Graphics) {
    stage = rhi::PipelineStage::FragmentShader;
  } else {
    ASSERT(0);
  }
  internal_reads_.emplace_back(
      NameAndAccess{name, stage, rhi::AccessFlags::ShaderStorageRead, RGResourceType::Texture});
  return rg_->get_resource(name, RGResourceType::Texture);
}

RGResourceHandle RGPass::w_tex(const std::string& name) {
  if (type_ != RGPassType::Compute && type_ != RGPassType::Transfer) {
    ALWAYS_ASSERT(0 && "Need compute pass to write texture");
  }
  RGAccess access{type_ == RGPassType::Compute ? RGAccess::ComputeWrite : RGAccess::TransferWrite};
  RGResourceHandle handle = rg_->get_resource(name, RGResourceType::Texture);
  ResourceAndUsage resource_usage{.handle = handle};
  convert_rg_access(access, resource_usage.access, resource_usage.stage);
  resource_usages_.push_back(resource_usage);
  return handle;
}

RGResourceHandle RGPass::read_tex(const std::string& name, RGAccess access) {
  RGResourceHandle handle = rg_->get_resource(name, RGResourceType::Texture);
  ResourceAndUsage resource_usage{.handle = handle};
  convert_rg_access(access, resource_usage.access, resource_usage.stage);
  resource_usages_.push_back(resource_usage);
  return handle;
}

RGResourceHandle RGPass::w_color_output(const std::string& name, const AttachmentInfo& att_info) {
  RGAccess access{RGAccess::ColorWrite};
  RGResourceHandle handle = rg_->add_tex_usage(name, att_info, access, *this);
  internal_writes_.emplace_back(NameAndAccess{name, rhi::PipelineStage::ColorAttachmentOutput,
                                              rhi::AccessFlags::ColorAttachmentWrite,
                                              RGResourceType::Texture});
  return handle;
}

RGResourceHandle RGPass::rw_color_output(const std::string& name, const std::string& input_name) {
  return rw_tex(name, input_name, rhi::PipelineStage::ColorAttachmentOutput,
                rhi::AccessFlags::ColorAttachmentRead | rhi::AccessFlags::ColorAttachmentWrite);
}

RGResourceHandle RGPass::rw_depth_output(const std::string& name, const std::string& input_name) {
  return rw_tex(name, input_name,
                rhi::PipelineStage::EarlyFragmentTests | rhi::PipelineStage::LateFragmentTests,
                rhi::AccessFlags::DepthStencilRead | rhi::AccessFlags::DepthStencilWrite);
}

RGResourceHandle RGPass::w_depth_output(const std::string& name, const AttachmentInfo& att_info) {
  RGAccess access{RGAccess::DepthStencilWrite};
  RGResourceHandle handle = rg_->add_tex_usage(name, att_info, access, *this);
  internal_writes_.emplace_back(NameAndAccess{
      name, rhi::PipelineStage::EarlyFragmentTests | rhi::PipelineStage::LateFragmentTests,
      rhi::AccessFlags::ColorAttachmentWrite, RGResourceType::Texture});
  return handle;
}

void RGPass::sample_external_tex(std::string name) {
  rhi::PipelineStage stage{};
  if (type_ == RGPassType::Compute) {
    stage = rhi::PipelineStage::ComputeShader;
  } else if (type_ == RGPassType::Graphics) {
    stage = rhi::PipelineStage::FragmentShader;
  } else {
    ASSERT(0);
  }
  sample_external_tex(std::move(name), stage);
}

void RGPass::sample_external_tex(std::string name, rhi::PipelineStage stage) {
  rg_->add_external_read_name(name);
  external_reads_.emplace_back(NameAndAccess{std::move(name), stage,
                                             rhi::AccessFlags::ShaderSampledRead,
                                             RGResourceType::ExternalTexture});
}

void RGPass::r_external_tex(std::string name) {
  rhi::PipelineStage stage{};
  if (type_ == RGPassType::Compute) {
    stage = rhi::PipelineStage::ComputeShader;
  } else if (type_ == RGPassType::Graphics) {
    stage = rhi::PipelineStage::FragmentShader;
  } else {
    ASSERT(0);
  }
  r_external_tex(std::move(name), stage);
}

void RGPass::r_external_tex(std::string name, rhi::PipelineStage stage) {
  rg_->add_external_read_name(name);
  external_reads_.emplace_back(NameAndAccess{std::move(name), stage,
                                             rhi::AccessFlags::ShaderStorageRead,
                                             RGResourceType::ExternalTexture});
}

void RenderGraph::add_external_write_usage(const std::string& name, rhi::TextureHandle tex_handle,
                                           RGPass& pass) {
  auto idx = external_textures_.size();
  external_textures_.emplace_back(tex_handle);
  external_name_to_handle_idx_.emplace(name, idx);
  auto [_it, success] = resource_use_name_to_writer_pass_idx_.emplace(name, pass.get_idx());
  ASSERT(success);
}

void RenderGraph::add_external_write_usage(const std::string& name, rhi::BufferHandle buf_handle,
                                           RGPass& pass) {
  auto idx = external_buffers_.size();
  external_buffers_.emplace_back(buf_handle);
  external_name_to_handle_idx_.emplace(name, idx);
  auto [_it, success] = resource_use_name_to_writer_pass_idx_.emplace(name, pass.get_idx());
  ASSERT(success);
}

void RGPass::w_external_tex_color_output(const std::string& name, rhi::TextureHandle tex_handle) {
  ASSERT(type_ == RGPassType::Graphics);
  // ASSERT(tex_handle.is_valid());
  rg_->add_external_write_usage(name, tex_handle, *this);
  external_writes_.emplace_back(name, rhi::PipelineStage::ColorAttachmentOutput,
                                rhi::AccessFlags::ColorAttachmentWrite,
                                RGResourceType::ExternalTexture);
}

void RGPass::w_external_tex(const std::string& name, rhi::TextureHandle tex_handle) {
  ALWAYS_ASSERT((type_ == RGPassType::Compute ||
                 type_ == RGPassType::Transfer &&
                     "Need compute or transfer pass to write external texture."));
  rg_->add_external_write_usage(name, tex_handle, *this);
  rhi::PipelineStage stage{};
  rhi::AccessFlags access{};
  if (type_ == RGPassType::Compute) {
    stage = rhi::PipelineStage::ComputeShader;
    access = rhi::AccessFlags::ShaderWrite;
  } else {
    stage = rhi::PipelineStage::AllTransfer;
    access = rhi::AccessFlags::TransferWrite;
  }
  external_writes_.emplace_back(name, stage, access, RGResourceType::ExternalTexture);
}

void RGPass::w_external_buf(const std::string& name, rhi::BufferHandle buf) {
  rhi::PipelineStage stage{};
  if (type_ == RGPassType::Compute) {
    stage = rhi::PipelineStage::ComputeShader;
  } else if (type_ == RGPassType::Transfer) {
    stage = rhi::PipelineStage::AllTransfer;
  } else if (type_ == RGPassType::Graphics) {
    stage = rhi::PipelineStage::AllGraphics;
  } else {
    ASSERT(0);
  }
  w_external_buf(name, buf, stage);
}

void RGPass::w_external_buf(const std::string& name, rhi::BufferHandle buf,
                            rhi::PipelineStage stage) {
  rhi::AccessFlags access{};
  rg_->add_external_write_usage(name, buf, *this);
  if (type_ == RGPassType::Compute || type_ == RGPassType::Graphics) {
    access = rhi::AccessFlags::ShaderWrite;
  } else {
    access = rhi::AccessFlags::TransferWrite;
  }
  external_writes_.emplace_back(name, stage, access, RGResourceType::ExternalBuffer);
}

void RGPass::r_external_buf(std::string name) {
  rhi::PipelineStage stage{};
  if (type_ == RGPassType::Compute) {
    stage = rhi::PipelineStage::ComputeShader;
  } else if (type_ == RGPassType::Graphics) {
    stage = rhi::PipelineStage::AllGraphics;
  } else if (type_ == RGPassType::Transfer) {
    stage = rhi::PipelineStage::AllTransfer;
  }
  r_external_buf(std::move(name), stage);
}

void RGPass::r_external_buf(std::string name, rhi::PipelineStage stage) {
  external_reads_.emplace_back(std::move(name), stage, rhi::AccessFlags::ShaderRead,
                               RGResourceType::ExternalBuffer);
}

void RGPass::rw_external_buf(std::string name, const std::string& input_name) {
  rhi::PipelineStage stage{};
  if (type_ == RGPassType::Compute) {
    stage = rhi::PipelineStage::ComputeShader;
  } else if (type_ == RGPassType::Graphics) {
    stage = rhi::PipelineStage::AllGraphics;
  } else if (type_ == RGPassType::Transfer) {
    stage = rhi::PipelineStage::AllTransfer;
  } else {
    ASSERT(0);
  }
  rw_external_buf(std::move(name), input_name, stage);
}

void RGPass::rw_external_buf(std::string name, const std::string& input_name,
                             rhi::PipelineStage stage) {
  uint32_t rw_read_name_i = add_read_write_resource(input_name);
  rg_->add_external_rw_buffer_usage(name, input_name, *this);
  external_reads_.emplace_back(std::move(name), stage,
                               rhi::AccessFlags::ShaderRead | rhi::AccessFlags::ShaderWrite,
                               RGResourceType::ExternalBuffer, rw_read_name_i);
}

void RenderGraph::add_external_rw_buffer_usage(const std::string& name,
                                               const std::string& input_name, RGPass& pass) {
  ASSERT(external_name_to_handle_idx_.contains(input_name));
  auto handle = external_name_to_handle_idx_.at(input_name);
  // TODO: reconsider whether this needs to be separate
  external_name_to_handle_idx_.emplace(name, handle);
  resource_use_name_to_writer_pass_idx_.emplace(name, pass.get_idx());
}

RGResourceHandle RenderGraph::get_resource(const std::string& name, RGResourceType type) {
  if (type == RGResourceType::Texture || type == RGResourceType::Buffer) {
    return resource_name_to_handle_.at(name);
  }
  ALWAYS_ASSERT(0);
  return {};
}

void RenderGraph::add_internal_rw_usage(const std::string& name, RGResourceHandle handle,
                                        Pass& pass) {
  resource_name_to_handle_.emplace(name, handle);
  resource_use_name_to_writer_pass_idx_.emplace(name, pass.get_idx());
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

RGResourceHandle RenderGraph::Pass::w_buf(const std::string& name, rhi::PipelineStage stage,
                                          size_t size, bool defer_reuse) {
  internal_writes_.emplace_back(
      NameAndAccess{name, stage, rhi::AccessFlags::ShaderStorageWrite, RGResourceType::Buffer});
  return rg_->add_buf_usage(name, {.size = size, .defer_reuse = defer_reuse}, *this);
}

RGResourceHandle RenderGraph::Pass::r_buf(const std::string& name, rhi::PipelineStage stage) {
  internal_reads_.emplace_back(
      NameAndAccess{name, stage, rhi::AccessFlags::ShaderStorageRead, RGResourceType::Buffer});
  return rg_->get_resource(name, RGResourceType::Buffer);
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

RGResourceHandle RenderGraph::Pass::rw_buf(const std::string& name, rhi::PipelineStage stage,
                                           const std::string& input_name) {
  auto handle = rg_->get_resource(input_name, RGResourceType::Buffer);
  uint32_t rw_read_name_i = add_read_write_resource(input_name);
  rg_->add_internal_rw_usage(name, handle, *this);
  auto name_access = NameAndAccess{
      name, stage, rhi::AccessFlags::ShaderStorageRead | rhi::AccessFlags::ShaderStorageWrite,
      RGResourceType::Buffer, rw_read_name_i};
  internal_writes_.emplace_back(name_access);
  internal_reads_.emplace_back(name_access);
  return handle;
}

uint32_t RenderGraph::Pass::add_read_write_resource(const std::string& name) {
  auto idx = rw_resource_read_names_.size();
  rw_resource_read_names_.emplace_back(name);
  return idx;
}

RGResourceHandle RenderGraph::Pass::rw_tex(const std::string& name, const std::string& input_name,
                                           rhi::PipelineStage stage, rhi::AccessFlags access) {
  RGResourceHandle handle = rg_->get_resource(input_name, RGResourceType::Texture);
  uint32_t rw_read_name_i = add_read_write_resource(input_name);
  rg_->add_internal_rw_usage(name, handle, *this);
  auto name_access = NameAndAccess{name, stage, access, RGResourceType::Texture, rw_read_name_i};
  internal_writes_.emplace_back(name_access);
  internal_reads_.emplace_back(name_access);
  return handle;
}

}  // namespace gfx

}  // namespace TENG_NAMESPACE
