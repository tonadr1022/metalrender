#pragma once

#include "hlsl/shared_globals.h"

namespace TENG_NAMESPACE {

namespace gfx {

struct RendererSettings {
  struct Pipeline {
    bool mesh_shaders_enabled = true;
    bool reverse_z = true;
  } pipeline;

  struct Culling {
    bool paused = false;
    bool enabled = true;
    bool meshlet_frustum = true;
    bool meshlet_cone = true;
    bool meshlet_occlusion = true;
    bool object_occlusion = false;
  } culling;

  struct Shadows {
    bool enabled = false;
  } shadows;

  struct Debug {
    DebugRenderMode render_mode = DebugRenderMode::None;
    int depth_pyramid_mip_view = 0;
  } debug;

  struct Ui {
    bool imgui_enabled = true;
  } ui;

  struct Developer {
    bool render_graph_verbose = false;
    bool collect_meshlet_draw_stats = true;
  } developer;
};

}  // namespace gfx

}  // namespace TENG_NAMESPACE
