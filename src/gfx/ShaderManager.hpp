#pragma once

#include <filesystem>

namespace gfx {

class ShaderManager {
 public:
  void init(const std::filesystem::path& shader_dir, const std::filesystem::path& shader_out_dir);

 private:
};

}  // namespace gfx
