#pragma once

#include <Metal/MTLComputePipeline.hpp>
#include <Metal/MTLRenderPipeline.hpp>

#include "core/Config.hpp"
#include "gfx/rhi/Pipeline.hpp"

namespace TENG_NAMESPACE {

namespace gfx::mtl {

struct Pipeline final : rhi::Pipeline {
  Pipeline(MTL::RenderPipelineState* render_pso, const rhi::GraphicsPipelineCreateInfo& cinfo)
      : rhi::Pipeline(cinfo), render_pso(render_pso) {}
  Pipeline(MTL::ComputePipelineState* compute_pso, const rhi::ShaderCreateInfo& cinfo)
      : rhi::Pipeline(cinfo), compute_pso(compute_pso) {}
  Pipeline() = default;
  ~Pipeline() override = default;

  MTL::RenderPipelineState* render_pso;
  MTL::ComputePipelineState* compute_pso;
  size_t render_target_info_hash;
};

}  // namespace gfx::mtl

}  // namespace TENG_NAMESPACE