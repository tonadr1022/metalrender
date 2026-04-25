#include "engine/render/DebugClearRenderer.hpp"

#include "engine/render/RenderFrameContext.hpp"
#include "engine/render/RenderScene.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/Swapchain.hpp"

namespace teng::engine {

DebugClearRenderer::DebugClearRenderer(glm::vec4 clear_color) : clear_color_(clear_color) {}

void DebugClearRenderer::render(RenderFrameContext& frame, const RenderScene& scene) {
  last_scene_ = &scene;

  auto& pass = frame.render_graph->add_graphics_pass("debug_clear");
  pass.w_swapchain_tex(frame.swapchain);
  pass.set_ex([device = frame.device, swapchain = frame.swapchain, clear_color = clear_color_](
                  gfx::rhi::CmdEncoder* enc) mutable {
    device->begin_swapchain_rendering(swapchain, enc, &clear_color);
    enc->end_rendering();
  });
}

}  // namespace teng::engine
