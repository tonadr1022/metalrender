#include "RenderGraph.hpp"

#include "core/EAssert.hpp"
#include "core/Logger.hpp"
#include "gfx/CmdEncoder.hpp"

namespace gfx {

namespace {
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

#define ACCESS_FLAG_OR(x, y) \
  (static_cast<rhi::AccessFlags>((x) | static_cast<rhi::AccessFlagsBits>(y)))

#define STAGE_OR(x, y) \
  (static_cast<rhi::PipelineStage>((x) | static_cast<rhi::PipelineStageBits>(y)))

void convert_rg_access(RGAccess access, rhi::AccessFlags& out_access,
                       rhi::PipelineStage& out_stages) {
  if (access & ColorWrite) {
    out_access = ACCESS_FLAG_OR(out_access, rhi::AccessFlags_ColorAttachmentWrite);
    out_stages = STAGE_OR(out_stages, rhi::PipelineStage_ColorAttachmentOutput);
  }
  if (access & ColorRead) {
    out_access = ACCESS_FLAG_OR(out_access, rhi::AccessFlags_ColorAttachmentRead);
    out_stages = STAGE_OR(out_stages, rhi::PipelineStage_ColorAttachmentOutput);
  }
  if (access & DepthStencilRead) {
    out_access = ACCESS_FLAG_OR(out_access, rhi::AccessFlags_DepthStencilRead);
    out_stages = STAGE_OR(
        out_stages, rhi::PipelineStage_EarlyFragmentTests | rhi::PipelineStage_LateFragmentTests);
  }
  if (access & DepthStencilWrite) {
    out_access = ACCESS_FLAG_OR(out_access, rhi::AccessFlags_DepthStencilWrite);
    out_stages = STAGE_OR(
        out_stages, rhi::PipelineStage_EarlyFragmentTests | rhi::PipelineStage_LateFragmentTests);
  }
  if (access & ComputeRead) {
    out_access = ACCESS_FLAG_OR(out_access, rhi::AccessFlags_ShaderRead);
    out_stages = STAGE_OR(out_stages, rhi::PipelineStage_ComputeShader);
  }
  if (access & ComputeWrite) {
    out_access = ACCESS_FLAG_OR(out_access, rhi::AccessFlags_ShaderWrite);
    out_stages = STAGE_OR(out_stages, rhi::PipelineStage_ComputeShader);
  }
  if (access & ComputeSample) {
    out_access = ACCESS_FLAG_OR(out_access, rhi::AccessFlags_ShaderSampledRead);
    out_stages = STAGE_OR(out_stages, rhi::PipelineStage_ComputeShader);
  }
}

#undef ACCESS_FLAG_OR
#undef STAGE_OR

}  // namespace

RGPass::RGPass(std::string name, RenderGraph* rg, uint32_t pass_i)
    : rg_(rg), pass_i_(pass_i), name_(std::move(name)) {}

RGPass& RenderGraph::add_pass(const std::string& name) {
  auto idx = static_cast<uint32_t>(passes_.size());
  passes_.emplace_back(RGPass{name, this, idx});
  return passes_.back();
}

void RenderGraph::execute() {}

void RenderGraph::reset() { passes_.clear(); }

void RenderGraph::bake(bool verbose) {
  if (verbose) {
    LINFO("//////////// Baking Render Graph ////////////");
  }

  // find sink nodes, ie nodes that don't write to anything
  sink_passes_.clear();
  ALWAYS_ASSERT(passes_.size() > 0);
  for (size_t pass_i = 0; pass_i < passes_.size(); pass_i++) {
    auto& pass = passes_[pass_i];
    if (pass.get_resource_writes().empty()) {
      ALWAYS_ASSERT((pass.get_resource_reads().size() || pass.get_resource_writes().size()) &&
                    "Pass must have at least one read or write");
      sink_passes_.emplace_back(pass_i);
    }
    ASSERT(pass.execute_fn_);
  }

  // traverse pass dependencies
  pass_dependencies_.resize(passes_.size());
  for (uint32_t pass_i : sink_passes_) {
    intermed_pass_stack_.push_back(pass_i);
    find_deps_recursive(pass_i, 0);
  }

  {
    // remove duplicates and assemble pass stack
    for (auto pass_i : intermed_pass_stack_) {
      if (intermed_pass_visited_.contains(pass_i)) {
        continue;
      }
      intermed_pass_visited_.insert(pass_i);
      pass_stack_.emplace_back(pass_i);
    }
  }

  if (verbose) {
    LINFO("Pass Order:");
    for (auto pass_i : pass_stack_) {
      LINFO("\t{}", passes_[pass_i].name_);
    }
    LINFO("");
  }
  if (verbose) {
    LINFO("//////////// Done Baking Render Graph ////////////");
  }
}

void RenderGraph::init() { passes_.reserve(200); }

RGResourceHandle RGPass::add(const std::string& name, AttachmentInfo att_info, RGAccess access) {
  assert_rg_access_valid(access);
  RGResourceHandle handle = rg_->add_tex_usage(name, att_info, access, *this);
  if (access & AnyRead) {
    resource_reads_.push_back(handle);
  }
  if (access & AnyWrite) {
    ALWAYS_ASSERT((!(access & AnyRead)) &&
                  "Cannot have a read and write access for single resource at single usage");
    resource_writes_.push_back(handle);
  }

  return handle;
}

RGResourceHandle RenderGraph::add_tex_usage(const std::string& name, const AttachmentInfo& att_info,
                                            RGAccess access, RGPass& pass) {
  rhi::AccessFlags access_bits{};
  rhi::PipelineStage stage_bits{};
  convert_rg_access(access, access_bits, stage_bits);

  auto resource_handle_it = resource_name_to_handle_.find(name);
  if (resource_handle_it != resource_name_to_handle_.end()) {
    auto handle = resource_handle_it->second;
    auto* usage = get_tex_usage(handle);
    usage->accesses |= access_bits;
    usage->stages |= stage_bits;
    return handle;
  }

  RGResourceHandle handle = {.idx = static_cast<uint32_t>(tex_usages_.size()),
                             .type = RGResourceType::Texture};

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
  for (const auto& resource : pass.get_resource_reads()) {
    auto& write_pass_indices = get_resource_write_pass_usages(resource);
    if (write_pass_indices.empty()) {
      LCRITICAL("RenderGraph: No pass writes to resource: type={}, idx={}",
                to_string(resource.type), resource.idx);
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
