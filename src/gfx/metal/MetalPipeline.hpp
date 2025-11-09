#pragma once

#include <Metal/MTLComputePipeline.hpp>
#include <Metal/MTLRenderPipeline.hpp>

#include "gfx/Pipeline.hpp"
#include "gfx/RendererTypes.hpp"

struct MetalPipeline final : rhi::Pipeline {
  MetalPipeline(MTL::RenderPipelineState* render_pso, MTL::ComputePipelineState* compute_pso)
      : render_pso(render_pso), compute_pso(compute_pso) {}
  MetalPipeline() = default;
  ~MetalPipeline() override = default;

  MTL::RenderPipelineState* render_pso{};
  MTL::ComputePipelineState* compute_pso{};
};
