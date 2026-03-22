#pragma once

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

namespace gfx {

struct RendererSettings {
  bool mesh_shaders_enabled = true;
  bool meshlet_frustum_culling_enabled = true;
  bool meshlet_cone_culling_enabled = true;
  bool meshlet_occlusion_culling_enabled = true;
  bool culling_enabled = true;
  bool reverse_z = true;
};

}  // namespace gfx

}  // namespace TENG_NAMESPACE