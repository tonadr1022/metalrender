#include "Pipeline.hpp"

namespace rhi {

const char* to_string(ShaderType type) {
  switch (type) {
    case ShaderType::Fragment:
      return "Fragment";
    case ShaderType::Vertex:
      return "Vertex";
    case ShaderType::Object:
      return "Object";
    case ShaderType::Mesh:
      return "Mesh";
    case ShaderType::Compute:
      return "Compute";
    case ShaderType::None:
      return "None";
  }
}

}  // namespace rhi
