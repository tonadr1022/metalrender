#include "Pipeline.hpp"

#include "core/Hash.hpp"

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

namespace rhi {

const char* to_string(ShaderType type) {
  switch (type) {
    case ShaderType::Fragment:
      return "Fragment";
    case ShaderType::Vertex:
      return "Vertex";
    case ShaderType::Task:
      return "Object";
    case ShaderType::Mesh:
      return "Mesh";
    case ShaderType::Compute:
      return "Compute";
    case ShaderType::None:
      return "None";
  }
}

size_t compute_render_target_info_hash(const rhi::RenderTargetInfo& render_target_info) {
  auto hash = (size_t)render_target_info.stencil_format;
  util::hash::hash_combine(hash, render_target_info.depth_format);
  for (const auto format : render_target_info.color_formats) {
    util::hash::hash_combine(hash, format);
  }
  return hash;
}
}  // namespace rhi

} // namespace TENG_NAMESPACE
