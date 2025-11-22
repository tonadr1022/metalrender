#include "RenderGraph.hpp"

#include <algorithm>

#include "core/EAssert.hpp"
#include "core/Logger.hpp"
#include "gfx/CmdEncoder.hpp"
#include "gfx/Device.hpp"

namespace gfx {

namespace {

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

bool is_access_read(rhi::AccessFlags access) {
  return access &
         (rhi::AccessFlags_IndirectCommandRead | rhi::AccessFlags_IndexRead |
          rhi::AccessFlags_VertexAttributeRead | rhi::AccessFlags_UniformRead |
          rhi::AccessFlags_InputAttachmentRead | rhi::AccessFlags_ShaderRead |
          rhi::AccessFlags_ColorAttachmentRead | rhi::AccessFlags_DepthStencilRead |
          rhi::AccessFlags_TransferRead | rhi::AccessFlags_HostRead | rhi::AccessFlags_MemoryRead |
          rhi::AccessFlags_ShaderSampledRead | rhi::AccessFlags_ShaderStorageRead);
}

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

void assert_rg_access_valid(RGAccess access) {
  if (access & AnyRead) {
    ALWAYS_ASSERT((!(access & AnyWrite)) &&
                  "Cannot have a read and write access for single resource at single usage");
  }
  if (access & AnyWrite) {
    ALWAYS_ASSERT((!(access & AnyRead)) &&
                  "Cannot have a read and write access for single resource at single usage");
  }
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

RGPass::RGPass(std::string name, RenderGraph* rg, uint32_t pass_i)
    : rg_(rg), pass_i_(pass_i), name_(std::move(name)) {}

RGPass& RenderGraph::add_pass(const std::string& name) {
  auto idx = static_cast<uint32_t>(passes_.size());
  passes_.emplace_back(name, this, idx);
  return passes_.back();
}

void RenderGraph::execute() {
  for (auto pass_i : pass_stack_) {
    rhi::CmdEncoder* enc = device_->begin_command_list();
    for (auto& barrier : pass_barrier_infos_[pass_i]) {
      enc->barrier(barrier.src_stage, barrier.src_access, barrier.dst_stage, barrier.dst_access);
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
  resource_pass_usages_[0].clear();
  resource_pass_usages_[1].clear();
}

void RenderGraph::reset() {}

void RenderGraph::bake(bool verbose) {
  if (verbose) {
    LINFO("//////////// Baking Render Graph ////////////");
  }
  {
    sink_passes_.clear();
    pass_dependencies_.clear();
    intermed_pass_visited_.clear();
    intermed_pass_stack_.clear();
    pass_stack_.clear();
  }

  // find sink nodes, ie nodes that don't write to anything
  sink_passes_.clear();
  ALWAYS_ASSERT(passes_.size() > 0);
  for (size_t pass_i = 0; pass_i < passes_.size(); pass_i++) {
    auto& pass = passes_[pass_i];
    if (!pass.has_resource_writes()) {
      if (pass.get_resource_usages().empty()) {
        LCRITICAL("Pass does not read or write to a resource: {}", pass.get_name());
        exit(1);
      }
      sink_passes_.emplace_back(pass_i);
    }
    if (!pass.get_execute_fn()) {
      LCRITICAL("No execute fn set for pass {}", pass.get_name());
      exit(1);
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
    // remove duplicates and assemble pass stack
    for (auto pass_i : intermed_pass_stack_) {
      ASSERT(pass_i < passes_.size());
      if (intermed_pass_visited_.contains(pass_i)) {
        continue;
      }
      intermed_pass_visited_.insert(pass_i);
      pass_stack_.emplace_back(pass_i);
    }
    std::ranges::reverse(pass_stack_);
  }

  if (verbose) {
    LINFO("Pass Order:");
    for (auto pass_i : pass_stack_) {
      auto& pass = passes_[pass_i];
      LINFO("\t{}", pass.get_name());
      for (const auto& resource_usage : pass.get_resource_usages()) {
        LINFO("{:<50}\t{:<50}\t{:<50}", get_resource_name(resource_usage.handle),
              rhi_access_to_string(resource_usage.access),
              rhi_pipeline_stage_to_string(resource_usage.stage));
      }
    }
    LINFO("");
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

    if (verbose) {
      LINFO("BARRIERS FOR PASS: {}", pass.get_name());
      if (barriers.empty()) {
        LINFO("\tNONE");
      }
      for (auto& barrier : barriers) {
        LINFO("RESOURCE: {}", get_resource_name(barrier.resource));
        LINFO("\tSRC_ACCESS: {}", rhi_access_to_string(barrier.src_access));
        LINFO("\tDST_ACCESS: {}", rhi_access_to_string(barrier.dst_access));
        LINFO("\tSRC_STAGE: {}", rhi_pipeline_stage_to_string(barrier.src_stage));
        LINFO("\tDST_STAGE: {}", rhi_pipeline_stage_to_string(barrier.dst_stage));
      }
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

RGResourceHandle RGPass::add_tex(const std::string& name, AttachmentInfo att_info,
                                 RGAccess access) {
  assert_rg_access_valid(access);
  RGResourceHandle handle = rg_->add_tex_usage(name, att_info, access, *this);
  ResourceAndUsage resource_usage{.handle = handle};
  convert_rg_access(access, resource_usage.access, resource_usage.stage);
  if (access & AnyRead) {
    resource_read_indices_.push_back(resource_usages_.size());
  }
  resource_usages_.push_back(resource_usage);

  return handle;
}

RGResourceHandle RGPass::add_tex(rhi::TextureHandle tex_handle, RGAccess access) {
  assert_rg_access_valid(access);
  RGResourceHandle resource_handle = rg_->add_tex_usage(tex_handle, access, *this);
  ResourceAndUsage resource_usage{.handle = resource_handle};
  convert_rg_access(access, resource_usage.access, resource_usage.stage);

  if (access & AnyRead) {
    resource_read_indices_.push_back(resource_usages_.size());
  }
  resource_usages_.push_back(resource_usage);

  return resource_handle;
}

RGResourceHandle RGPass::add_buf(const std::string& name, rhi::BufferHandle buf_handle,
                                 RGAccess access) {
  assert_rg_access_valid(access);
  RGResourceHandle resource_handle = rg_->add_buf_usage(name, buf_handle, access, *this);
  ResourceAndUsage resource_usage{.handle = resource_handle};
  convert_rg_access(access, resource_usage.access, resource_usage.stage);

  if (access & AnyRead) {
    resource_read_indices_.push_back(resource_usages_.size());
  }
  resource_usages_.push_back(resource_usage);

  return resource_handle;
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
  resource_handle_to_name_.emplace(resource_handle.to_64(), name);
  resource_name_to_handle_.emplace(name, resource_handle.to_64());

  // auto* usage = get_tex_usage(resource_handle);
  // usage->accesses |= access_bits;
  // usage->stages |= stage_bits;

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
  if (resource_handle_it != resource_name_to_handle_.end()) {
    auto handle = resource_handle_it->second;
    // auto* usage = get_tex_usage(handle);
    // usage->accesses |= access_bits;
    // usage->stages |= stage_bits;
    return handle;
  }

  RGResourceHandle handle = {.idx = static_cast<uint32_t>(tex_usages_.size()),
                             .type = RGResourceType::Texture};

  resource_handle_to_name_.emplace(handle.to_64(), name);
  resource_name_to_handle_.emplace(name, handle);

  TextureUsage tex_use{.att_info = att_info};
  emplace_back_tex_usage(tex_use);

  if (access & AnyRead) {
    add_resource_to_pass_reads(handle, pass);
  }
  if (access & AnyWrite) {
    add_resource_to_pass_writes(handle, pass);
  }

  return handle;
}

RGResourceHandle RenderGraph::add_buf_usage(const std::string& name, rhi::BufferHandle buf_handle,
                                            RGAccess access, RGPass& pass) {
  rhi::AccessFlags access_bits{};
  rhi::PipelineStage stage_bits{};
  convert_rg_access(access, access_bits, stage_bits);

  auto resource_handle_it = resource_name_to_handle_.find(name);
  if (resource_handle_it != resource_name_to_handle_.end()) {
    auto handle = resource_handle_it->second;
    // auto* usage = get_tex_usage(handle);
    // usage->accesses |= access_bits;
    // usage->stages |= stage_bits;
    return handle;
  }

  RGResourceHandle handle = {.idx = static_cast<uint32_t>(buf_usages_.size()),
                             .type = RGResourceType::Buffer};

  resource_handle_to_name_.emplace(handle.to_64(), name);
  resource_name_to_handle_.emplace(name, handle);

  BufferUsage buf_usage{.handle = buf_handle};
  emplace_back_buf_usage(buf_usage);

  if (access & AnyRead) {
    add_resource_to_pass_reads(handle, pass);
  }
  if (access & AnyWrite) {
    add_resource_to_pass_writes(handle, pass);
  }

  return handle;
}

RenderGraph::ResourceUsage* RenderGraph::get_resource_usage(RGResourceHandle handle) {
  if (handle.type == RGResourceType::Texture) {
    ALWAYS_ASSERT(handle.idx < tex_usages_.size());
    return &tex_usages_[handle.idx];
  }
  ALWAYS_ASSERT(handle.idx < buf_usages_.size());
  return &buf_usages_[handle.idx];
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
  if (stack_size > passes_.size() * 2) {
    LCRITICAL("RenderGraph: Cycle");
    exit(1);
  }
  auto& pass = passes_[pass_i];
  stack_size++;
  for (uint32_t resource_i : pass.get_resource_reads()) {
    const auto& resource = pass.get_resource_usages()[resource_i];
    auto& write_pass_indices = get_resource_write_pass_usages(resource.handle);
    if (write_pass_indices.empty()) {
      LCRITICAL("RenderGraph: No pass writes to resource: type={}, idx={}",
                to_string(resource.handle.type), resource.handle.idx);
      exit(1);
    }
    for (auto write_pass_i : write_pass_indices) {
      intermed_pass_stack_.emplace_back(write_pass_i);
      pass_dependencies_[pass_i].insert(write_pass_i);
      find_deps_recursive(write_pass_i, stack_size);
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

}  // namespace gfx

#undef ACCESS_FLAG_OR
#undef STAGE_OR
