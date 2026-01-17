#include "RenderGraph.hpp"

#include <ranges>

#include "core/EAssert.hpp"
#include "core/Logger.hpp"
#include "gfx/CmdEncoder.hpp"
#include "gfx/Device.hpp"

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
  if (stage & rhi::PipelineStage_TopOfPipe) {
    result += "PipelineStage_TopOfPipe | ";
  }
  if (stage & rhi::PipelineStage_DrawIndirect) {
    result += "PipelineStage_DrawIndirect | ";
  }
  if (stage & rhi::PipelineStage_VertexInput) {
    result += "PipelineStage_VertexInput | ";
  }
  if (stage & rhi::PipelineStage_VertexShader) {
    result += "PipelineStage_VertexShader | ";
  }
  if (stage & rhi::PipelineStage_FragmentShader) {
    result += "PipelineStage_FragmentShader | ";
  }
  if (stage & rhi::PipelineStage_EarlyFragmentTests) {
    result += "PipelineStage_EarlyFragmentTests | ";
  }
  if (stage & rhi::PipelineStage_LateFragmentTests) {
    result += "PipelineStage_LateFragmentTests | ";
  }
  if (stage & rhi::PipelineStage_ColorAttachmentOutput) {
    result += "PipelineStage_ColorAttachmentOutput | ";
  }
  if (stage & rhi::PipelineStage_ComputeShader) {
    result += "PipelineStage_ComputeShader | ";
  }
  if (stage & rhi::PipelineStage_AllTransfer) {
    result += "PipelineStage_AllTransfer | ";
  }
  if (stage & rhi::PipelineStage_BottomOfPipe) {
    result += "PipelineStage_BottomOfPipe | ";
  }
  if (stage & rhi::PipelineStage_Host) {
    result += "PipelineStage_Host | ";
  }
  if (stage & rhi::PipelineStage_AllGraphics) {
    result += "PipelineStage_AllGraphics | ";
  }
  if (stage & rhi::PipelineStage_AllCommands) {
    result += "PipelineStage_AllCommands | ";
  }
  if (result.size()) {
    result = result.substr(0, result.size() - 3);
  }
  return result;
}

bool is_access_read(rhi::AccessFlags access) { return access & rhi::AccessFlags_AnyRead; }
bool is_access_write(RGAccess access) { return access & RGAccess::AnyWrite; }

std::string rhi_access_to_string(rhi::AccessFlags access) {
  std::string result;
  if (access & rhi::AccessFlags_IndirectCommandRead) {
    result += "IndirectCommandRead | ";
  }
  if (access & rhi::AccessFlags_IndexRead) {
    result += "IndexRead | ";
  }
  if (access & rhi::AccessFlags_VertexAttributeRead) {
    result += "VertexAttributeRead | ";
  }
  if (access & rhi::AccessFlags_UniformRead) {
    result += "UniformRead | ";
  }
  if (access & rhi::AccessFlags_InputAttachmentRead) {
    result += "InputAttachmentRead | ";
  }
  if (access & rhi::AccessFlags_ShaderRead) {
    result += "ShaderRead | ";
  }
  if (access & rhi::AccessFlags_ShaderWrite) {
    result += "ShaderWrite | ";
  }
  if (access & rhi::AccessFlags_ColorAttachmentRead) {
    result += "ColorAttachmentRead | ";
  }
  if (access & rhi::AccessFlags_ColorAttachmentWrite) {
    result += "ColorAttachmentWrite | ";
  }
  if (access & rhi::AccessFlags_DepthStencilRead) {
    result += "DepthStencilRead | ";
  }
  if (access & rhi::AccessFlags_DepthStencilWrite) {
    result += "DepthStencilWrite | ";
  }
  if (access & rhi::AccessFlags_TransferRead) {
    result += "TransferRead | ";
  }
  if (access & rhi::AccessFlags_TransferWrite) {
    result += "TransferWrite | ";
  }
  if (access & rhi::AccessFlags_HostRead) {
    result += "HostRead | ";
  }
  if (access & rhi::AccessFlags_HostWrite) {
    result += "HostWrite | ";
  }
  if (access & rhi::AccessFlags_MemoryRead) {
    result += "MemoryRead | ";
  }
  if (access & rhi::AccessFlags_MemoryWrite) {
    result += "MemoryWrite | ";
  }
  if (access & rhi::AccessFlags_ShaderSampledRead) {
    result += "ShaderSampledRead | ";
  }
  if (access & rhi::AccessFlags_ShaderStorageRead) {
    result += "ShaderStorageRead | ";
  }
  if (access & rhi::AccessFlags_ShaderStorageWrite) {
    result += "ShaderStorageWrite | ";
  }
  if (result.size()) {
    result = result.substr(0, result.size() - 3);
  }
  return result;
}

void assert_rg_access_valid(RGAccess) {
  // if (access & AnyRead) {
  //   ALWAYS_ASSERT((!(access & AnyWrite)) &&
  //                 "Cannot have a read and write access for single resource at single usage");
  // }
  // if (access & AnyWrite) {
  //   ALWAYS_ASSERT((!(access & AnyRead)) &&
  //                 "Cannot have a read and write access for single resource at single usage");
  // }
}

template <typename Flags, typename Bits>
constexpr Flags flag_or(Flags x, Bits y) noexcept {
  return static_cast<Flags>(static_cast<std::underlying_type_t<Flags>>(x) |
                            static_cast<uint64_t>(y));
}

void convert_rg_access(RGAccess access, rhi::AccessFlags& out_access,
                       rhi::PipelineStage& out_stages) {
  if (access & ColorWrite) {
    out_access = flag_or(out_access, rhi::AccessFlags_ColorAttachmentWrite);
    out_stages = flag_or(out_stages, rhi::PipelineStage_ColorAttachmentOutput);
  }
  if (access & ShaderRead) {
    out_access = flag_or(out_access, rhi::AccessFlags_ShaderRead);
    out_stages = flag_or(out_stages, rhi::PipelineStage_AllGraphics);
  }
  if (access & ColorRead) {
    out_access = flag_or(out_access, rhi::AccessFlags_ColorAttachmentRead);
    out_stages = flag_or(out_stages, rhi::PipelineStage_ColorAttachmentOutput);
  }
  if (access & DepthStencilRead) {
    out_access = flag_or(out_access, rhi::AccessFlags_DepthStencilRead);
    out_stages = flag_or(
        out_stages, rhi::PipelineStage_EarlyFragmentTests | rhi::PipelineStage_LateFragmentTests);
  }
  if (access & DepthStencilWrite) {
    out_access = flag_or(out_access, rhi::AccessFlags_DepthStencilWrite);
    out_stages = flag_or(
        out_stages, rhi::PipelineStage_EarlyFragmentTests | rhi::PipelineStage_LateFragmentTests);
  }
  if (access & VertexRead) {
    out_access = flag_or(out_access, rhi::AccessFlags_ShaderStorageRead);
    out_stages = flag_or(out_stages, rhi::PipelineStage_VertexShader);
  }
  if (access & IndexRead) {
    out_access = flag_or(out_access, rhi::AccessFlags_ShaderStorageRead);
    out_stages = flag_or(out_stages, rhi::PipelineStage_VertexShader);
  }
  if (access & IndirectRead) {
    out_access = flag_or(out_access, rhi::AccessFlags_IndirectCommandRead);
    out_stages = flag_or(out_stages, rhi::PipelineStage_DrawIndirect);
  }
  if (access & ComputeRead) {
    out_access = flag_or(out_access, rhi::AccessFlags_ShaderRead);
    out_stages = flag_or(out_stages, rhi::PipelineStage_ComputeShader);
  }
  if (access & ComputeWrite) {
    out_access = flag_or(out_access, rhi::AccessFlags_ShaderWrite);
    out_stages = flag_or(out_stages, rhi::PipelineStage_ComputeShader);
  }
  if (access & ComputeSample) {
    out_access = flag_or(out_access, rhi::AccessFlags_ShaderSampledRead);
    out_stages = flag_or(out_stages, rhi::PipelineStage_ComputeShader);
  }
  if (access & FragmentSample) {
    out_access = flag_or(out_access, rhi::AccessFlags_ShaderSampledRead);
    out_stages = flag_or(out_stages, rhi::PipelineStage_FragmentShader);
  }
  if (access & FragmentStorageRead) {
    out_access = flag_or(out_access, rhi::AccessFlags_ShaderStorageRead);
    out_stages = flag_or(out_stages, rhi::PipelineStage_FragmentShader);
  }
  if (access & TransferWrite) {
    out_access = flag_or(out_access, rhi::AccessFlags_TransferWrite);
    out_stages = flag_or(out_stages, rhi::PipelineStage_AllTransfer);
  }
  if (access & TransferRead) {
    out_access = flag_or(out_access, rhi::AccessFlags_TransferRead);
    out_stages = flag_or(out_stages, rhi::PipelineStage_AllTransfer);
  }
}

ResourceAndUsage create_resource_usage(RGResourceHandle handle, RGAccess access) {
  ResourceAndUsage resource_usage{.handle = handle};
  convert_rg_access(access, resource_usage.access, resource_usage.stage);
  return resource_usage;
}

}  // namespace

RGPass::RGPass(std::string name, RenderGraph* rg, uint32_t pass_i, RGPassType type)
    : rg_(rg), pass_i_(pass_i), name_(std::move(name)), type_(type) {}

RGPass& RenderGraph::add_pass(const std::string& name, RGPassType type) {
  auto idx = static_cast<uint32_t>(passes_.size());
  passes_.emplace_back(name, this, idx, type);
  return passes_.back();
}

void RenderGraph::execute() {
  for (auto pass_i : pass_stack_) {
    rhi::CmdEncoder* enc = device_->begin_command_list();
    for (auto& barrier : pass_barrier_infos_[pass_i]) {
      if (barrier.resource.type == RGResourceType::Buffer) {
        auto buf = get_buf_usage(barrier.resource)->handle;
        enc->barrier(buf, barrier.src_stage, barrier.src_access, barrier.dst_stage,
                     barrier.dst_access);
      } else {
        enc->barrier(barrier.src_stage, barrier.src_access, barrier.dst_stage, barrier.dst_access);
      }
    }

    auto& pass = passes_[pass_i];
    pass.get_execute_fn()(enc);
    enc->end_encoding();
  }
  {
    passes_.clear();
    for (auto& b : pass_barrier_infos_) {
      b.clear();
    }
    external_texture_count_ = 0;
  }

  tex_usages_.clear();
  buf_usages_.clear();
  resource_name_to_handle_.clear();
  resource_handle_to_name_.clear();
  tex_handle_to_handle_.clear();
  resource_pass_usages_[0].clear();
  resource_pass_usages_[1].clear();
  resource_read_name_to_writer_pass_.clear();

  rg_resource_handle_to_actual_att_.clear();
}

void RenderGraph::reset() {}

void RenderGraph::bake(glm::uvec2 fb_size, bool verbose) {
  if (verbose) {
    LINFO("//////////// Baking Render Graph ////////////");
  }
  {
    sink_passes_.clear();
    pass_dependencies_.clear();
    intermed_pass_visited_.clear();
    intermed_pass_stack_.clear();
    pass_stack_.clear();

    free_atts_.clear();
    {
      static std::vector<size_t> del_indices;
      size_t i = 0;
      for (const auto& a : actual_atts_) {
        ASSERT(!a.att_info.is_swapchain_tex);
        if (a.att_info.size_class == SizeClass::Swapchain && a.att_info.dims != fb_size) {
          del_indices.emplace_back(i);
        } else {
          free_atts_[a.att_info].emplace_back(a.tex_handle.handle);
        }
        i++;
      }

      for (unsigned int i : std::ranges::reverse_view(del_indices)) {
        actual_atts_[i] = std::move(actual_atts_.back());
        actual_atts_.pop_back();
      }
      del_indices.clear();
    }

    // clear old swapchain images if swapchain resized
    std::vector<rhi::TextureHandle> stale_img_handles;
    for (const auto& a : actual_atts_) {
      if (a.att_info.size_class != SizeClass::Swapchain) {
        continue;
      }
      ASSERT(!a.att_info.is_swapchain_tex);
      if (a.att_info.dims != fb_size) {
        stale_img_handles.emplace_back(a.tex_handle.handle);
      }
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
    for (const auto& use : pass.get_resource_usages()) {
      if (get_resource_read_pass_usages(use.handle).empty()) {
        sink = true;
        break;
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

  // traverse pass dependencies
  for (auto& p : pass_dependencies_) {
    p.clear();
  }
  pass_dependencies_.resize(passes_.size());
  for (uint32_t pass_i : sink_passes_) {
    intermed_pass_stack_.push_back(pass_i);
    find_deps_recursive(pass_i, 0);
  }
  {
    // TODO: cache/reserve
    std::unordered_set<uint32_t> curr_stack_passes;
    std::unordered_set<uint32_t> visited_passes;

    std::function<void(uint32_t)> dfs = [&](uint32_t pass) {
      if (curr_stack_passes.contains(pass)) {
        ASSERT(0 && "Cycle detected");
      }
      if (visited_passes.contains(pass)) return;

      curr_stack_passes.insert(pass);

      for (const auto& dep : pass_dependencies_[pass]) {
        dfs(dep);
      }

      curr_stack_passes.erase(pass);
      visited_passes.insert(pass);
      pass_stack_.push_back(pass);
    };

    pass_stack_.clear();
    visited_passes.clear();

    for (uint32_t root : intermed_pass_stack_) {
      dfs(root);
    }
  }

  if (verbose) {
    LINFO("//////////////// Pass Order ////////////////");
    for (auto pass_i : pass_stack_) {
      auto& pass = passes_[pass_i];
      LINFO("[PASS]: {}", pass.get_name());
      for (const auto& resource_usage : pass.get_resource_usages()) {
        LINFO("{:<50}\t{:<50}\t{:<50}", get_resource_name(resource_usage.handle),
              rhi_access_to_string(resource_usage.access),
              rhi_pipeline_stage_to_string(resource_usage.stage));
      }
    }
    LINFO("");
  }

  // create attachment images
  for (const auto& usage : tex_usages_) {
    if (usage.handle.is_valid() || usage.att_info.is_swapchain_tex) {
      continue;
    }
    auto att_info = usage.att_info;
    if (att_info.size_class == SizeClass::Swapchain) {
      att_info.dims = fb_size;
    }
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
      glm::uvec2 dims = att_info.dims;
      if (att_info.size_class == SizeClass::Swapchain) {
        dims = fb_size;
      }
      auto att_tx_handle = device_->create_tex_h(rhi::TextureDesc{
          .format = att_info.format,
          .usage = is_depth_format(att_info.format) ? rhi::TextureUsageColorAttachment
                                                    : rhi::TextureUsageDepthStencilAttachment,
          .dims = glm::uvec3{dims.x, dims.y, 1},
          .mip_levels = att_info.mip_levels,
          .array_length = att_info.array_layers,
          .bindless = true,
          .name = "render_graph_tex_att"});
      actual_att_handle = att_tx_handle.handle;
      actual_atts_.emplace_back(att_info, std::move(att_tx_handle));
    }

    for (const auto& handle : usage.handles_using) {
      rg_resource_handle_to_actual_att_.emplace(handle.to64(), actual_att_handle);
    }
  }

  struct ResourceState {
    rhi::AccessFlags access;
    rhi::PipelineStage stage;
  };
  // TODO: move
  std::vector<ResourceState> tex_states_(tex_usages_.size());
  std::vector<ResourceState> buf_states_(buf_usages_.size());

  auto get_resource_state = [&tex_states_,
                             &buf_states_](RGResourceHandle handle) -> ResourceState& {
    return handle.type == RGResourceType::Texture ? tex_states_[handle.idx]
                                                  : buf_states_[handle.idx];
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
    for (const auto& resource_usage : pass.get_resource_usages()) {
      // record writes for future passes use as src stage/acesses.
      if (!is_access_read(resource_usage.access)) {
        auto& resource_state = get_resource_state(resource_usage.handle);
        // TODO: don't or the access, just set?
        resource_state.access = flag_or(resource_state.access, resource_usage.access);
        resource_state.stage = flag_or(resource_state.stage, resource_usage.stage);
      }
    }

    for (uint32_t resource_read : pass.get_resource_reads()) {
      const auto& resource_usage = pass.get_resource_usages()[resource_read];
      barriers.emplace_back(BarrierInfo{
          .resource = resource_usage.handle,
          .src_stage = get_resource_state(resource_usage.handle).stage,
          .dst_stage = resource_usage.stage,
          .src_access = get_resource_state(resource_usage.handle).access,
          .dst_access = resource_usage.access,
      });
    }

#define CLR_CYAN "\033[36m"
#define CLR_PURPLE "\033[35m"
#define CLR_GREEN "\033[32m"
#define CLR_RESET "\033[0m"

    if (verbose) {
      LINFO(CLR_GREEN "{} Pass" CLR_RESET ": {}", to_string(pass.type()), pass.get_name());
      for (auto& barrier : barriers) {
        LINFO(CLR_PURPLE "RESOURCE: {}" CLR_RESET "", get_resource_name(barrier.resource));
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

RGResourceHandle RenderGraph::add_tex_usage(rhi::TextureHandle tex_handle, RGAccess access,
                                            RGPass& pass) {
  rhi::AccessFlags access_bits{};
  rhi::PipelineStage stage_bits{};
  convert_rg_access(access, access_bits, stage_bits);
  RGResourceHandle resource_handle;
  auto resource_handle_it = tex_handle_to_handle_.find(tex_handle.to64());
  if (resource_handle_it != tex_handle_to_handle_.end()) {
    resource_handle = resource_handle_it->second;
  } else {
    resource_handle = {.idx = static_cast<uint32_t>(tex_usages_.size()),
                       .type = RGResourceType::Texture};
    TextureUsage tex_use{.handle = tex_handle};
    emplace_back_tex_usage(tex_use);
    tex_handle_to_handle_.emplace(tex_handle.to64(), resource_handle);
  }

  std::string name = "external_texture_" + std::to_string(external_texture_count_++);
  resource_handle_to_name_.emplace(resource_handle.to64(), name);
  resource_name_to_handle_.emplace(name, resource_handle.to64());

  if (access & AnyRead) {
    add_resource_to_pass_reads(resource_handle, pass);
  }
  if (access & AnyWrite) {
    add_resource_to_pass_writes(resource_handle, pass);
  }
  return resource_handle;
}

RGResourceHandle RenderGraph::add_tex_usage(const std::string& name, const AttachmentInfo& att_info,
                                            RGAccess access, RGPass& pass) {
  rhi::AccessFlags access_bits{};
  rhi::PipelineStage stage_bits{};
  convert_rg_access(access, access_bits, stage_bits);

  auto resource_handle_it = resource_name_to_handle_.find(name);
  RGResourceHandle handle{};
  if (resource_handle_it != resource_name_to_handle_.end()) {
    handle = resource_handle_it->second;
  } else {
    ASSERT((att_info.is_swapchain_tex || att_info.format != rhi::TextureFormat::Undefined));
    handle = {.idx = static_cast<uint32_t>(tex_usages_.size()), .type = RGResourceType::Texture};
    TextureUsage tex_use{.att_info = att_info};
    emplace_back_tex_usage(tex_use);
    tex_usages_.back().handles_using.emplace_back(handle);
  }
  ALWAYS_ASSERT(tex_usages_[handle.idx].handles_using.size() <= 1);

  resource_handle_to_name_.emplace(handle.to64(), name);
  resource_name_to_handle_.emplace(name, handle);

  if (access & AnyRead) {
    add_resource_to_pass_reads(handle, pass);
  }
  if (access & AnyWrite) {
    add_resource_to_pass_writes(handle, pass);
  }

  return handle;
}

RGResourceHandle RenderGraph::add_buf_usage(std::string name, rhi::BufferHandle buf_handle,
                                            RGAccess access, RGPass& pass,
                                            const std::string& input_name) {
  rhi::AccessFlags access_bits{};
  rhi::PipelineStage stage_bits{};
  convert_rg_access(access, access_bits, stage_bits);

  // Add the input name to this resource pass' reads if applicable
  if (!input_name.empty()) {
    ASSERT(is_access_write(access));
    auto resource_handle_it = resource_name_to_handle_.find(input_name);
    ALWAYS_ASSERT(resource_handle_it != resource_name_to_handle_.end());
    auto handle = resource_handle_it->second;
    buf_usages_[handle.idx].name_stack.emplace_back(name);
    add_resource_to_pass_reads(handle, pass);
  }

  if (access_bits & rhi::AccessFlags_AnyWrite) {
    resource_read_name_to_writer_pass_.emplace(name, pass.get_idx());
  }

  auto resource_handle_it = resource_name_to_handle_.find(name);
  if (resource_handle_it != resource_name_to_handle_.end()) {
    auto handle = resource_handle_it->second;
    if (access & AnyRead) {
      add_resource_to_pass_reads(handle, pass);
    }
    if (access & AnyWrite) {
      add_resource_to_pass_writes(handle, pass);
    }

    return handle;
  }

  if (!input_name.empty()) {
    auto resource_handle_it = resource_name_to_handle_.find(input_name);

    ALWAYS_ASSERT(resource_handle_it != resource_name_to_handle_.end());
    auto handle = resource_handle_it->second;
    // TODO: multiple names per handle
    resource_handle_to_name_.emplace(handle.to64(), name);
    resource_name_to_handle_.emplace(name, handle);
    if (access & AnyRead) {
      add_resource_to_pass_reads(handle, pass);
    }
    if (access & AnyWrite) {
      add_resource_to_pass_writes(handle, pass);
    }

    return handle;
  }

  RGResourceHandle handle = {.idx = static_cast<uint32_t>(buf_usages_.size()),
                             .type = RGResourceType::Buffer};

  resource_handle_to_name_.emplace(handle.to64(), name);
  resource_name_to_handle_.emplace(name, handle);

  emplace_back_buf_usage(buf_handle, std::vector<std::string>{name});

  if (access & AnyRead) {
    add_resource_to_pass_reads(handle, pass);
  }
  if (access & AnyWrite) {
    add_resource_to_pass_writes(handle, pass);
  }

  return handle;
}

RenderGraph::TextureUsage* RenderGraph::get_tex_usage(RGResourceHandle handle) {
  ALWAYS_ASSERT(handle.type == RGResourceType::Texture);
  ALWAYS_ASSERT(handle.idx < tex_usages_.size());
  return &tex_usages_[handle.idx];
}

RenderGraph::BufferUsage* RenderGraph::get_buf_usage(RGResourceHandle handle) {
  ALWAYS_ASSERT(handle.type == RGResourceType::Buffer);
  ALWAYS_ASSERT(handle.idx < buf_usages_.size());
  return &buf_usages_[handle.idx];
}

void RenderGraph::find_deps_recursive(uint32_t pass_i, uint32_t stack_size) {
  if (stack_size > passes_.size() * 100) {
    ALWAYS_ASSERT(0 && "RenderGraph: Cycle");
  }
  if (intermed_pass_visited_.contains(pass_i)) {
    return;
  }
  intermed_pass_visited_.insert(pass_i);
  auto& pass = passes_[pass_i];
  stack_size++;
  for (uint32_t resource_i : pass.get_resource_reads()) {
    const auto& resource = pass.get_resource_usages()[resource_i];
    const std::string& read_name = pass.get_resource_read_names()[resource_i];
    // TODO: clean this BS up pls
    if (!read_name.empty()) {
      auto writer_pass_it = resource_read_name_to_writer_pass_.find(read_name);
      ALWAYS_ASSERT(writer_pass_it != resource_read_name_to_writer_pass_.end());
      uint32_t write_pass_i = writer_pass_it->second;
      intermed_pass_stack_.emplace_back(write_pass_i);
      pass_dependencies_[pass_i].insert(write_pass_i);
      find_deps_recursive(write_pass_i, stack_size);
    } else {
      // here: the read needs a name so the write with the matching name can be found
      // only one pass can write to this instance of a read.
      auto& write_pass_indices = get_resource_write_pass_usages(resource.handle);
      if (write_pass_indices.empty()) {
        LCRITICAL("RenderGraph: No pass writes to resource: type={}, idx={}",
                  to_string(resource.handle.type), resource.handle.idx);
        exit(1);
      }
      for (auto write_pass_i : write_pass_indices) {
        if (write_pass_i == pass_i) {
          continue;
        }
        intermed_pass_stack_.emplace_back(write_pass_i);
        pass_dependencies_[pass_i].insert(write_pass_i);
        find_deps_recursive(write_pass_i, stack_size);
      }
    }
  }
}

const char* to_string(RGResourceType type) {
  switch (type) {
    case gfx::RGResourceType::Texture:
      return "Texture";
    case gfx::RGResourceType::Buffer:
      return "Buffer";
  }
}

rhi::TextureHandle RenderGraph::get_att_img(RGResourceHandle tex_handle) {
  auto it = rg_resource_handle_to_actual_att_.find(tex_handle.to64());
  ALWAYS_ASSERT(it != rg_resource_handle_to_actual_att_.end());
  ASSERT(it->second.is_valid());
  return it->second;
}

RGResourceHandle RGPass::sample_tex(const std::string& name) {
  return read_tex(
      name, type_ == RGPassType::Compute ? RGAccess::ComputeSample : RGAccess::FragmentSample);
}

RGResourceHandle RGPass::read_tex(const std::string& name) {
  return read_tex(
      name, type_ == RGPassType::Compute ? RGAccess::ComputeRead : RGAccess::FragmentStorageRead);
}

RGResourceHandle RGPass::write_tex(const std::string& name) {
  if (type_ != RGPassType::Compute && type_ != RGPassType::Transfer) {
    ALWAYS_ASSERT(0 && "Need compute pass to write texture");
  }
  RGAccess access{type_ == RGPassType::Compute ? RGAccess::ComputeWrite : RGAccess::TransferWrite};
  RGResourceHandle handle = rg_->add_tex_usage(name, access, *this);
  ResourceAndUsage resource_usage{.handle = handle};
  convert_rg_access(access, resource_usage.access, resource_usage.stage);
  resource_usages_.push_back(resource_usage);
  resource_read_names_.emplace_back();
  return handle;
}

RGResourceHandle RGPass::read_tex(const std::string& name, RGAccess access) {
  RGResourceHandle handle = rg_->add_tex_usage(name, access, *this);
  ResourceAndUsage resource_usage{.handle = handle};
  convert_rg_access(access, resource_usage.access, resource_usage.stage);
  if (access & AnyRead) {
    resource_read_indices_.push_back(resource_usages_.size());
  }
  resource_usages_.push_back(resource_usage);
  resource_read_names_.emplace_back();

  return handle;
}

RGResourceHandle RGPass::add_color_output(const std::string& name, const AttachmentInfo& att_info) {
  RGAccess access{RGAccess::ColorWrite};
  assert_rg_access_valid(access);
  RGResourceHandle handle = rg_->add_tex_usage(name, att_info, access, *this);
  resource_usages_.push_back(create_resource_usage(handle, access));
  resource_read_names_.emplace_back();
  return handle;
}

RGResourceHandle RGPass::add_depth_output(const std::string& name, const AttachmentInfo& att_info) {
  RGAccess access{RGAccess::DepthStencilWrite};
  assert_rg_access_valid(access);
  RGResourceHandle handle = rg_->add_tex_usage(name, att_info, access, *this);
  resource_usages_.push_back(create_resource_usage(handle, access));
  resource_read_names_.emplace_back();
  return handle;
}

RGResourceHandle RenderGraph::add_tex_usage(const std::string& name, RGAccess access,
                                            RGPass& pass) {
  rhi::AccessFlags access_bits{};
  rhi::PipelineStage stage_bits{};
  convert_rg_access(access, access_bits, stage_bits);
  auto resource_handle_it = resource_name_to_handle_.find(name);
  ALWAYS_ASSERT(resource_handle_it != resource_name_to_handle_.end());
  auto handle = resource_handle_it->second;

  if (access & AnyRead) {
    add_resource_to_pass_reads(handle, pass);
  }
  if (access & AnyWrite) {
    add_resource_to_pass_writes(handle, pass);
  }
  return handle;
}

RGResourceHandle RGPass::read_write_buf(const std::string& name, rhi::BufferHandle buf_handle,
                                        const std::string& input_name) {
  ALWAYS_ASSERT(type_ == RGPassType::Compute);
  RGAccess access{RGAccess::ComputeRW};
  RGResourceHandle resource_handle =
      rg_->add_buf_usage(name, buf_handle, access, *this, input_name);
  resource_read_indices_.push_back(resource_usages_.size());
  resource_read_names_.emplace_back(input_name);
  resource_usages_.push_back(create_resource_usage(resource_handle, access));
  return resource_handle;
}

RGResourceHandle RGPass::write_buf(const std::string& name, rhi::BufferHandle buf_handle) {
  ALWAYS_ASSERT(type_ == RGPassType::Compute);
  RGAccess access{RGAccess::ComputeWrite};
  RGResourceHandle resource_handle = rg_->add_buf_usage(name, buf_handle, access, *this, "");
  resource_read_names_.emplace_back();
  resource_usages_.push_back(create_resource_usage(resource_handle, access));
  return resource_handle;
}

void RGPass::read_buf(const std::string& name) {
  RGAccess access{type_ == RGPassType::Compute ? RGAccess::ComputeRead : RGAccess::ShaderRead};
  RGResourceHandle resource_handle = rg_->add_buf_read_usage(name, access, *this);
  resource_read_indices_.push_back(resource_usages_.size());
  resource_read_names_.emplace_back(name);
  resource_usages_.push_back(create_resource_usage(resource_handle, access));
}

void RGPass::read_indirect_buf(const std::string& name) {
  ALWAYS_ASSERT(type_ == RGPassType::Graphics);
  RGAccess access{RGAccess::IndirectRead};
  RGResourceHandle resource_handle = rg_->add_buf_read_usage(name, access, *this);
  resource_read_indices_.push_back(resource_usages_.size());
  resource_read_names_.emplace_back(name);
  resource_usages_.push_back(create_resource_usage(resource_handle, access));
}

RGResourceHandle RenderGraph::add_buf_read_usage(const std::string& name, RGAccess access,
                                                 RGPass& pass) {
  ALWAYS_ASSERT(access & AnyRead);
  auto resource_handle_it = resource_name_to_handle_.find(name);
  ALWAYS_ASSERT(resource_handle_it != resource_name_to_handle_.end());
  auto handle = resource_handle_it->second;
  add_resource_to_pass_reads(handle, pass);
  return handle;
}

RGResourceHandle RGPass::sample_external_tex(rhi::TextureHandle tex_handle) {
  ALWAYS_ASSERT(type_ == RGPassType::Graphics);
  RGAccess access{RGAccess::FragmentSample};
  RGResourceHandle resource_handle = rg_->add_tex_usage(tex_handle, access, *this);
  resource_read_indices_.push_back(resource_usages_.size());
  resource_usages_.push_back(create_resource_usage(resource_handle, access));
  resource_read_names_.emplace_back();

  return resource_handle;
}

RGResourceHandle RGPass::write_external_tex(rhi::TextureHandle tex_handle) {
  if (type_ != RGPassType::Compute && type_ != RGPassType::Transfer) {
    ALWAYS_ASSERT(0 && "Need compute pass to write texture");
  }
  RGAccess access{type_ == RGPassType::Compute ? RGAccess::ComputeWrite : RGAccess::TransferWrite};
  // TODO: use other func
  RGResourceHandle handle = rg_->add_tex_usage(tex_handle, access, *this);
  ResourceAndUsage resource_usage{.handle = handle};
  convert_rg_access(access, resource_usage.access, resource_usage.stage);
  resource_usages_.push_back(resource_usage);
  resource_read_names_.emplace_back();
  return handle;
}

RGResourceHandle RenderGraph::add_external_tex_write_usage(const std::string& name,
                                                           rhi::TextureHandle tex_handle,
                                                           RGAccess access, RGPass& pass) {
  rhi::AccessFlags access_bits{};
  rhi::PipelineStage stage_bits{};
  convert_rg_access(access, access_bits, stage_bits);

  auto resource_handle_it = resource_name_to_handle_.find(name);
  ALWAYS_ASSERT(resource_handle_it == resource_name_to_handle_.end());
  RGResourceHandle resource_handle{.idx = static_cast<uint32_t>(tex_usages_.size()),
                                   .type = RGResourceType::Texture};
  TextureUsage tex_use{.att_info = {}, .handle = tex_handle};
  emplace_back_tex_usage(tex_use);
  tex_handle_to_handle_.emplace(tex_handle.to64(), resource_handle);

  resource_handle_to_name_.emplace(resource_handle.to64(), name);
  resource_name_to_handle_.emplace(name, resource_handle.to64());

  if (access & AnyRead) {
    add_resource_to_pass_reads(resource_handle, pass);
  }
  if (access & AnyWrite) {
    add_resource_to_pass_writes(resource_handle, pass);
  }
  return resource_handle;
}

RGResourceHandle RGPass::write_external_tex(const std::string& name,
                                            rhi::TextureHandle tex_handle) {
  if (type_ != RGPassType::Compute && type_ != RGPassType::Transfer) {
    ALWAYS_ASSERT(0 && "Need compute pass to write texture");
  }
  RGAccess access{type_ == RGPassType::Compute ? RGAccess::ComputeWrite : RGAccess::TransferWrite};
  RGResourceHandle handle = rg_->add_external_tex_write_usage(name, tex_handle, access, *this);
  ResourceAndUsage resource_usage{.handle = handle};
  convert_rg_access(access, resource_usage.access, resource_usage.stage);
  resource_usages_.push_back(resource_usage);
  resource_read_names_.emplace_back();
  return handle;
}
}  // namespace gfx
