#pragma once

#include <filesystem>
#include <unordered_set>

#include "gfx/Pipeline.hpp"

namespace rhi {
struct GraphicsPipelineCreateInfo;
struct ShaderCreateInfo;
}  // namespace rhi

namespace gfx {

class ShaderManager {
 public:
  ShaderManager(const ShaderManager&) = delete;
  ShaderManager(ShaderManager&&) = delete;
  ShaderManager& operator=(const ShaderManager&) = delete;
  ShaderManager& operator=(ShaderManager&&) = delete;
  ShaderManager() = default;
  void init(rhi::Device* device, const std::filesystem::path& shader_dir,
            const std::filesystem::path& shader_out_dir);
  void shutdown();
  bool shader_dirty(const std::filesystem::path& path);
  rhi::PipelineHandleHolder create_graphics_pipeline(const rhi::GraphicsPipelineCreateInfo& cinfo);
  rhi::PipelineHandleHolder create_compute_pipeline(const rhi::ShaderCreateInfo& cinfo);
  bool compile_shader(const std::filesystem::path& path);

 private:
  std::filesystem::path get_shader_path(const std::string& relative_path, rhi::ShaderType type);

  // std::unordered_set<std::filesystem::path> dirty_shaders_;
  std::unordered_set<std::filesystem::path> clean_shaders_;
  std::filesystem::path shader_out_dir_;
  std::filesystem::path depfile_dir_;
  std::filesystem::path hashes_filepath_;
  rhi::Device* device_{};
};

}  // namespace gfx
