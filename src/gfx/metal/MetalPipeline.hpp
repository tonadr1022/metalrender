#pragma once

#include <Metal/MTLComputePipeline.hpp>
#include <Metal/MTLRenderPipeline.hpp>

struct MetalPipeline {
  MTL::RenderPipelineState* render_pso{};
  MTL::ComputePipelineState* compute_pso{};
};
