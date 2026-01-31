#pragma once

#include <Metal/MTLComputePipeline.hpp>
#include <Metal/MTLRenderPipeline.hpp>

#include "gfx/rhi/Pipeline.hpp"

struct MetalPipeline final : rhi::Pipeline {
  MetalPipeline(MTL::RenderPipelineState* render_pso, const rhi::GraphicsPipelineCreateInfo& cinfo)
      : rhi::Pipeline(cinfo), render_pso(render_pso) {}
  MetalPipeline(MTL::ComputePipelineState* compute_pso, const rhi::ShaderCreateInfo& cinfo)
      : rhi::Pipeline(cinfo), compute_pso(compute_pso) {}
  MetalPipeline() = default;
  ~MetalPipeline() override = default;

  MTL::RenderPipelineState* render_pso;
  MTL::ComputePipelineState* compute_pso;
  size_t render_target_info_hash;
};
