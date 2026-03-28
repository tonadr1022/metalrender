#pragma once

#include <filesystem>

#include "core/CVar.hpp"

namespace TENG_NAMESPACE {
namespace gfx {
namespace renderer_cv {

extern AutoCVarInt pipeline_mesh_shaders;
extern AutoCVarInt culling_paused;
extern AutoCVarInt culling_enabled;
extern AutoCVarInt culling_meshlet_frustum;
extern AutoCVarInt culling_meshlet_cone;
extern AutoCVarInt culling_meshlet_occlusion;
extern AutoCVarInt culling_object_occlusion;
extern AutoCVarInt shadows_enabled;
extern AutoCVarInt debug_render_mode;
extern AutoCVarInt debug_depth_pyramid_mip;
extern AutoCVarInt ui_imgui_enabled;
extern AutoCVarInt developer_render_graph_verbose;
extern AutoCVarInt developer_collect_meshlet_draw_stats;

}  // namespace renderer_cv

void init_renderer_cvars_from_startup(bool device_mesh_shaders_capable,
                                      const std::filesystem::path& config_file_path);

}  // namespace gfx
}  // namespace TENG_NAMESPACE
