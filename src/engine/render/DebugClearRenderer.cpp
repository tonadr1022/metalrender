#include "engine/render/DebugClearRenderer.hpp"

#include "engine/render/RenderFrameContext.hpp"
#include "engine/render/RenderScene.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "gfx/rhi/Swapchain.hpp"
#include "glm/ext/vector_float4.hpp"

namespace teng::engine {

DebugClearRenderer::DebugClearRenderer(glm::vec4 clear_color) : clear_color_(clear_color) {}

void DebugClearRenderer::render(RenderFrameContext& frame, const RenderScene& scene,
                                const SceneRenderView& view) {
  last_scene_ = &scene;
  last_view_ = view;

  auto& pass = frame.render_graph->add_graphics_pass("debug_clear");
  pass.w_swapchain_tex(frame.swapchain);
  pass.set_ex(
      [swapchain = frame.swapchain, clear_color = clear_color_](gfx::rhi::CmdEncoder* enc) mutable {
        enc->begin_rendering({
            gfx::rhi::RenderAttInfo::color_att(swapchain->get_current_texture(),
                                               gfx::rhi::LoadOp::Clear,
                                               gfx::rhi::ClearValue{.color = clear_color}),
        });
        enc->end_rendering();
      });
}

}  // namespace teng::engine
