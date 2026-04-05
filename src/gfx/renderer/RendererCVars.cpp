#include "gfx/renderer/RendererCVars.hpp"

namespace TENG_NAMESPACE {
namespace gfx {
namespace renderer_cv {

AutoCVarInt pipeline_mesh_shaders{
    "renderer.pipeline.mesh_shaders",
    "Use mesh-shader path when supported (requires restart if toggled).", 0,
    CVarFlags::EditCheckbox};
AutoCVarInt culling_paused{"renderer.culling.paused", "Freeze culling updates.", 0,
                           CVarFlags::EditCheckbox};
AutoCVarInt culling_enabled{"renderer.culling.enabled", "Master switch for culling.", 1,
                            CVarFlags::EditCheckbox};
AutoCVarInt culling_meshlet_frustum{"renderer.culling.meshlet_frustum", "Meshlet frustum culling.",
                                    1, CVarFlags::EditCheckbox};
AutoCVarInt culling_meshlet_cone{"renderer.culling.meshlet_cone", "Meshlet cone culling.", 1,
                                 CVarFlags::EditCheckbox};
AutoCVarInt culling_meshlet_occlusion{"renderer.culling.meshlet_occlusion",
                                      "Meshlet occlusion culling (depth pyramid).", 1,
                                      CVarFlags::EditCheckbox};
AutoCVarInt culling_object_occlusion{"renderer.culling.object_occlusion",
                                     "Object-level occlusion culling.", 1, CVarFlags::EditCheckbox};
AutoCVarInt shadows_enabled{"renderer.shadows.enabled", "Enable shadow mapping.", 0,
                            CVarFlags::EditCheckbox};
AutoCVarInt debug_render_mode{"renderer.debug.render_mode",
                              "Debug visualization mode (see DebugRenderMode).", 0,
                              CVarFlags::Advanced};
AutoCVarInt ui_imgui_enabled{"renderer.ui.imgui", "Draw ImGui overlay.", 0,
                             CVarFlags::EditCheckbox};
AutoCVarInt developer_render_graph_verbose{
    "renderer.developer.render_graph_verbose", "Verbose RenderGraph bake logging.", 0,
    static_cast<CVarFlags>(static_cast<uint16_t>(CVarFlags::EditCheckbox) |
                           static_cast<uint16_t>(CVarFlags::Advanced))};
AutoCVarInt developer_collect_meshlet_draw_stats{
    "renderer.developer.collect_meshlet_draw_stats", "Record meshlet draw statistics for readback.",
    1,
    static_cast<CVarFlags>(static_cast<uint16_t>(CVarFlags::EditCheckbox) |
                           static_cast<uint16_t>(CVarFlags::Advanced))};

}  // namespace renderer_cv

void apply_renderer_cvar_device_constraints(bool device_mesh_shaders_capable) {
  using util::hash::HashedString;

  if (!device_mesh_shaders_capable) {
    renderer_cv::pipeline_mesh_shaders.set(0);
    CVarSystem::get().merge_cvar_flags(HashedString{"renderer.pipeline.mesh_shaders"},
                                       CVarFlags::EditReadOnly);
  }
}

}  // namespace gfx
}  // namespace TENG_NAMESPACE
