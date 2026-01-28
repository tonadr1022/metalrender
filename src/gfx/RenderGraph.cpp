#include "RenderGraph.hpp"

#include <ranges>
#include <tracy/Tracy.hpp>
#include <utility>

#include "core/EAssert.hpp"
#include "core/Logger.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/Texture.hpp"

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
  if (stage & rhi::PipelineStage_TaskShader) {
    result += "PipelineStage_TaskShader | ";
  }
  if (stage & rhi::PipelineStage_MeshShader) {
    result += "PipelineStage_MeshShader | ";
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

bool is_access_write(rhi::AccessFlags access) { return access & rhi::AccessFlags_AnyWrite; }

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

}  // namespace

RGPass::RGPass(std::string name, RenderGraph* rg, uint32_t pass_i, RGPassType type)
    : rg_(rg), pass_i_(pass_i), name_(std::move(name)), type_(type) {}

RGPass& RenderGraph::add_pass(const std::string& name, RGPassType type) {
  auto idx = static_cast<uint32_t>(passes_.size());
  passes_.emplace_back(name, this, idx, type);
  return passes_.back();
}

void RenderGraph::execute() {
  ZoneScoped;
  for (auto pass_i : pass_stack_) {
    rhi::CmdEncoder* enc = device_->begin_command_list();
    for (auto& barrier : pass_barrier_infos_[pass_i]) {
      // TODO: barrier for external buffers
      ASSERT(barrier.resource.type != RGResourceType::Buffer);
      if (barrier.resource.type == RGResourceType::Buffer) {
        enc->barrier({}, barrier.src_stage, barrier.src_access, barrier.dst_stage,
                     barrier.dst_access);
      } else {
        enc->barrier(barrier.src_stage, barrier.src_access, barrier.dst_stage, barrier.dst_access);
      }
    }

    auto& pass = passes_[pass_i];
    enc->set_debug_name(pass.get_name().c_str());
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

  for (size_t i = 0; i < tex_att_infos_.size(); i++) {
    free_atts_[tex_att_infos_[i]].emplace_back(tex_att_handles_[i]);
  }
  tex_att_infos_.clear();

  resource_name_to_handle_.clear();
  resource_handle_to_name_.clear();
  tex_handle_to_handle_.clear();
  resource_pass_usages_[0].clear();
  resource_pass_usages_[1].clear();
  resource_read_name_to_writer_pass_.clear();
  external_name_to_handle_idx_.clear();
  resource_use_name_to_writer_pass_idx_.clear();
  external_buffers_.clear();
  external_textures_.clear();
}

void RenderGraph::reset() {}

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
    for (const auto& write : pass.get_external_writes()) {
      if (!external_read_names.contains(write.name)) {
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
      const std::vector<RGPass::NameAndAccess>* arrays[4] = {
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
          .usage = is_depth_format(att_info.format) ? rhi::TextureUsageColorAttachment
                                                    : rhi::TextureUsageDepthStencilAttachment,
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

  struct ResourceState {
    rhi::AccessFlags access;
    rhi::PipelineStage stage;
  };

  static std::vector<ResourceState> states[4];
  for (auto& state : states) state.clear();
  states[(int)RGResourceType::Texture].resize(tex_att_infos_.size());
  states[(int)RGResourceType::Buffer].resize(0);
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
      auto& resource_state = get_resource_state(rg_resource_handle);
      resource_state.access = flag_or(resource_state.access, write_use.acc);
      resource_state.stage = flag_or(resource_state.stage, write_use.stage);
    }

    for (const auto& write_use : pass.get_internal_writes()) {
      auto rg_resource_handle = resource_name_to_handle_.at(write_use.name);
      auto& resource_state = get_resource_state(rg_resource_handle);
      if (write_use.acc & rhi::AccessFlags_AnyWrite) {
        // add barrier
        const auto& state = get_resource_state(rg_resource_handle);
        barriers.emplace_back(BarrierInfo{
            .resource = rg_resource_handle,
            .src_stage = state.stage,
            .dst_stage = write_use.stage,
            .src_access = state.access,
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
              barrier.debug_name.size() ? barrier.debug_name : get_resource_name(barrier.resource));
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
  auto resource_handle_it = resource_name_to_handle_.find(name);
  ALWAYS_ASSERT(resource_handle_it == resource_name_to_handle_.end());
  RGResourceHandle handle{};
  ASSERT((att_info.is_swapchain_tex || att_info.format != rhi::TextureFormat::Undefined));
  handle = {.idx = static_cast<uint32_t>(tex_att_infos_.size()), .type = RGResourceType::Texture};
  tex_att_infos_.emplace_back(att_info);
  resource_use_name_to_writer_pass_idx_.emplace(name, pass.get_idx());
  resource_name_to_handle_.emplace(name, handle);
  return handle;
}

AttachmentInfo* RenderGraph::get_tex_att_info(RGResourceHandle handle) {
  ALWAYS_ASSERT(handle.type == RGResourceType::Texture);
  ALWAYS_ASSERT(handle.idx < tex_att_infos_.size());
  return &tex_att_infos_[handle.idx];
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
  return tex_att_handles_[tex_handle.idx];
}

RGResourceHandle RGPass::sample_tex(const std::string& name) {
  ASSERT(!name.empty());
  rhi::PipelineStage stage{};
  if (type_ == RGPassType::Compute) {
    stage = rhi::PipelineStage_ComputeShader;
  } else if (type_ == RGPassType::Graphics) {
    stage = rhi::PipelineStage_FragmentShader;
  } else {
    ASSERT(0);
  }
  internal_reads_.emplace_back(
      NameAndAccess{name, stage, rhi::AccessFlags_ShaderSampledRead, RGResourceType::Texture});
  return rg_->get_resource(name, RGResourceType::Texture);
}

RGResourceHandle RGPass::r_tex(const std::string& name) {
  rhi::PipelineStage stage{};
  if (type_ == RGPassType::Compute) {
    stage = rhi::PipelineStage_ComputeShader;
  } else if (type_ == RGPassType::Graphics) {
    stage = rhi::PipelineStage_FragmentShader;
  } else {
    ASSERT(0);
  }
  internal_reads_.emplace_back(
      NameAndAccess{name, stage, rhi::AccessFlags_ShaderStorageRead, RGResourceType::Texture});
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
  internal_writes_.emplace_back(NameAndAccess{name, rhi::PipelineStage_ColorAttachmentOutput,
                                              rhi::AccessFlags_ColorAttachmentWrite,
                                              RGResourceType::Texture});
  return handle;
}

RGResourceHandle RGPass::rw_color_output(const std::string& name, const std::string& input_name) {
  RGResourceHandle handle = rg_->get_resource(input_name, RGResourceType::Texture);
  uint32_t rw_read_name_i = rw_resource_read_names_.size();
  rw_resource_read_names_.emplace_back(input_name);
  rg_->add_internal_rw_tex_usage(name, input_name, *this);
  internal_writes_.emplace_back(
      NameAndAccess{name, rhi::PipelineStage_ColorAttachmentOutput,
                    (rhi::AccessFlags)(rhi::AccessFlags_ColorAttachmentRead |
                                       rhi::AccessFlags_ColorAttachmentWrite),
                    RGResourceType::Texture, rw_read_name_i});
  return handle;
}

RGResourceHandle RGPass::rw_depth_output(const std::string& name, const std::string& input_name) {
  RGResourceHandle handle = rg_->get_resource(input_name, RGResourceType::Texture);
  uint32_t rw_read_name_i = rw_resource_read_names_.size();
  rw_resource_read_names_.emplace_back(input_name);
  rg_->add_internal_rw_tex_usage(name, input_name, *this);
  internal_writes_.emplace_back(NameAndAccess{
      name,
      (rhi::PipelineStage)(rhi::PipelineStage_EarlyFragmentTests |
                           rhi::PipelineStage_LateFragmentTests),
      (rhi::AccessFlags)(rhi::AccessFlags_DepthStencilRead | rhi::AccessFlags_DepthStencilWrite),
      RGResourceType::Texture, rw_read_name_i});
  return handle;
}

RGResourceHandle RGPass::w_depth_output(const std::string& name, const AttachmentInfo& att_info) {
  RGAccess access{RGAccess::DepthStencilWrite};
  RGResourceHandle handle = rg_->add_tex_usage(name, att_info, access, *this);
  internal_writes_.emplace_back(
      NameAndAccess{name,
                    (rhi::PipelineStage)(rhi::PipelineStage_EarlyFragmentTests |
                                         rhi::PipelineStage_LateFragmentTests),
                    rhi::AccessFlags_ColorAttachmentWrite, RGResourceType::Texture});
  return handle;
}

void RGPass::sample_external_tex(std::string name) {
  rhi::PipelineStage stage{};
  if (type_ == RGPassType::Compute) {
    stage = rhi::PipelineStage_ComputeShader;
  } else if (type_ == RGPassType::Graphics) {
    stage = rhi::PipelineStage_FragmentShader;
  } else {
    ASSERT(0);
  }
  sample_external_tex(std::move(name), stage);
}

void RGPass::sample_external_tex(std::string name, rhi::PipelineStage stage) {
  rg_->external_read_names.insert(name);
  external_reads_.emplace_back(NameAndAccess{
      std::move(name), stage, rhi::AccessFlags_ShaderSampledRead, RGResourceType::ExternalTexture});
}

void RGPass::r_external_tex(std::string name) {
  rhi::PipelineStage stage{};
  if (type_ == RGPassType::Compute) {
    stage = rhi::PipelineStage_ComputeShader;
  } else if (type_ == RGPassType::Graphics) {
    stage = rhi::PipelineStage_FragmentShader;
  } else {
    ASSERT(0);
  }
  r_external_tex(std::move(name), stage);
}

void RGPass::r_external_tex(std::string name, rhi::PipelineStage stage) {
  rg_->external_read_names.insert(name);
  external_reads_.emplace_back(NameAndAccess{
      std::move(name), stage, rhi::AccessFlags_ShaderStorageRead, RGResourceType::ExternalTexture});
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
  rg_->add_external_write_usage(name, tex_handle, *this);
  external_writes_.emplace_back(name, rhi::PipelineStage_ColorAttachmentOutput,
                                rhi::AccessFlags_ColorAttachmentWrite,
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
    stage = rhi::PipelineStage_ComputeShader;
    access = rhi::AccessFlags_ShaderWrite;
  } else {
    stage = rhi::PipelineStage_AllTransfer;
    access = rhi::AccessFlags_TransferWrite;
  }
  external_writes_.emplace_back(name, stage, access, RGResourceType::ExternalTexture);
}

void RGPass::w_external(const std::string& name, rhi::BufferHandle buf) {
  rhi::PipelineStage stage{};
  if (type_ == RGPassType::Compute) {
    stage = rhi::PipelineStage_ComputeShader;
  } else if (type_ == RGPassType::Transfer) {
    stage = rhi::PipelineStage_AllTransfer;
  } else if (type_ == RGPassType::Graphics) {
    stage = rhi::PipelineStage_AllGraphics;
  } else {
    ASSERT(0);
  }
  w_external(name, buf, stage);
}

void RGPass::w_external(const std::string& name, rhi::BufferHandle buf, rhi::PipelineStage stage) {
  rhi::AccessFlags access{};
  rg_->add_external_write_usage(name, buf, *this);
  if (type_ == RGPassType::Compute || type_ == RGPassType::Graphics) {
    access = rhi::AccessFlags_ShaderWrite;
  } else {
    access = rhi::AccessFlags_TransferWrite;
  }
  external_writes_.emplace_back(name, stage, access, RGResourceType::ExternalBuffer);
}

void RGPass::r_external_buf(std::string name) {
  rhi::PipelineStage stage{};
  if (type_ == RGPassType::Compute) {
    stage = rhi::PipelineStage_ComputeShader;
  } else if (type_ == RGPassType::Graphics) {
    stage = rhi::PipelineStage_AllGraphics;
  } else {
    ASSERT(0);
  }
  r_external_buf(std::move(name), stage);
}

void RGPass::r_external_buf(std::string name, rhi::PipelineStage stage) {
  external_reads_.emplace_back(std::move(name), stage, rhi::AccessFlags_ShaderRead,
                               RGResourceType::ExternalBuffer);
}

void RGPass::rw_external_buf(std::string name, const std::string& input_name) {
  rhi::PipelineStage stage{};
  if (type_ == RGPassType::Compute) {
    stage = rhi::PipelineStage_ComputeShader;
  } else if (type_ == RGPassType::Graphics) {
    stage = rhi::PipelineStage_AllGraphics;
  } else if (type_ == RGPassType::Transfer) {
    stage = rhi::PipelineStage_AllTransfer;
  } else {
    ASSERT(0);
  }
  rw_external_buf(std::move(name), input_name, stage);
}

void RGPass::rw_external_buf(std::string name, const std::string& input_name,
                             rhi::PipelineStage stage) {
  uint32_t rw_read_name_i = rw_resource_read_names_.size();
  rw_resource_read_names_.emplace_back(input_name);
  rg_->add_external_rw_buffer_usage(name, input_name, *this);
  external_reads_.emplace_back(
      std::move(name), stage,
      (rhi::AccessFlags)(rhi::AccessFlags_ShaderRead | rhi::AccessFlags_ShaderWrite),
      RGResourceType::ExternalBuffer, rw_read_name_i);
}

void RenderGraph::add_external_rw_buffer_usage(const std::string& name,
                                               const std::string& input_name, RGPass& pass) {
  ASSERT(external_name_to_handle_idx_.contains(input_name));
  auto idx = external_name_to_handle_idx_.at(input_name);
  external_name_to_handle_idx_.emplace(name, idx);
  resource_use_name_to_writer_pass_idx_.emplace(name, pass.get_idx());
}

RGResourceHandle RenderGraph::get_resource(const std::string& name, RGResourceType type) {
  if (type == RGResourceType::Texture) {
    return resource_name_to_handle_.at(name);
  }
  ALWAYS_ASSERT(0);
  return {};
}

void RenderGraph::add_internal_rw_tex_usage(const std::string& name, const std::string& input_name,
                                            RGPass& pass) {
  resource_name_to_handle_.emplace(name, get_resource(input_name, RGResourceType::Texture));
  resource_use_name_to_writer_pass_idx_.emplace(name, pass.get_idx());
}

}  // namespace gfx
