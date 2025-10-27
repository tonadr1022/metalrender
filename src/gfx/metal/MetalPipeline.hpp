#pragma once

#include <Metal/MTLComputePipeline.hpp>
#include <Metal/MTLRenderPipeline.hpp>

#include "gfx/Pipeline.hpp"
#include "gfx/RendererTypes.hpp"

struct MetalPipeline : rhi::Pipeline {
  MTL::RenderPipelineState* render_pso{};
  MTL::ComputePipelineState* compute_pso{};
};
