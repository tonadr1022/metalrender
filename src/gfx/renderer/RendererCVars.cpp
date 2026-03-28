#include "gfx/renderer/RendererCVars.hpp"

#include <fstream>
#include <string>

#include "core/StringUtil.hpp"

namespace TENG_NAMESPACE {
namespace gfx {
namespace renderer_cv {

AutoCVarInt pipeline_mesh_shaders{"renderer.pipeline.mesh_shaders",
                                  "Use mesh-shader path when supported (requires restart if toggled "
                                  "from a state that skipped pipeline creation).",
                                  1, CVarFlags::EditCheckbox};
AutoCVarInt culling_paused{"renderer.culling.paused", "Freeze culling updates.", 0,
                           CVarFlags::EditCheckbox};
AutoCVarInt culling_enabled{"renderer.culling.enabled", "Master switch for culling.", 1,
                            CVarFlags::EditCheckbox};
AutoCVarInt culling_meshlet_frustum{"renderer.culling.meshlet_frustum",
                                    "Meshlet frustum culling.", 1, CVarFlags::EditCheckbox};
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
AutoCVarInt debug_depth_pyramid_mip{"renderer.debug.depth_pyramid_mip",
                                    "Mip level for depth pyramid debug view.", 0,
                                    CVarFlags::Advanced};
AutoCVarInt ui_imgui_enabled{"renderer.ui.imgui", "Draw ImGui overlay.", 1,
                             CVarFlags::EditCheckbox};
AutoCVarInt developer_render_graph_verbose{
    "renderer.developer.render_graph_verbose", "Verbose RenderGraph bake logging.", 0,
    static_cast<CVarFlags>(static_cast<uint16_t>(CVarFlags::EditCheckbox) |
                           static_cast<uint16_t>(CVarFlags::Advanced))};
AutoCVarInt developer_collect_meshlet_draw_stats{
    "renderer.developer.collect_meshlet_draw_stats",
    "Record meshlet draw statistics for readback.", 1,
    static_cast<CVarFlags>(static_cast<uint16_t>(CVarFlags::EditCheckbox) |
                           static_cast<uint16_t>(CVarFlags::Advanced))};

}  // namespace renderer_cv

void init_renderer_cvars_from_startup(bool device_mesh_shaders_capable,
                                      const std::filesystem::path& config_file_path) {
  using util::hash::HashedString;

  int32_t mesh = device_mesh_shaders_capable ? 1 : 0;
  constexpr const char* key_mesh_shaders_enabled = "mesh_shaders_enabled";

  std::ifstream file(config_file_path);
  if (file.is_open()) {
    std::string line;
    while (std::getline(file, line)) {
      auto kv = core::split_string_at_first(line, '=');
      if (kv.first == key_mesh_shaders_enabled) {
        mesh = kv.second == "1" ? 1 : 0;
      }
    }
  }

  if (!device_mesh_shaders_capable) {
    mesh = 0;
  }
  renderer_cv::pipeline_mesh_shaders.set(mesh);
  if (!device_mesh_shaders_capable) {
    CVarSystem::get().merge_cvar_flags(HashedString{"renderer.pipeline.mesh_shaders"},
                                       CVarFlags::EditReadOnly);
  }
}

}  // namespace gfx
}  // namespace TENG_NAMESPACE
