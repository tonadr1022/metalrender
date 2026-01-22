#pragma once

#include <filesystem>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gfx/Pipeline.hpp"

namespace rhi {
struct GraphicsPipelineCreateInfo;
struct ShaderCreateInfo;
}  // namespace rhi

namespace gfx {

class ShaderManager {
 public:
  using HashT = uint64_t;
  ShaderManager(const ShaderManager&) = delete;
  ShaderManager(ShaderManager&&) = delete;
  ShaderManager& operator=(const ShaderManager&) = delete;
  ShaderManager& operator=(ShaderManager&&) = delete;
  ShaderManager() = default;
  void init(rhi::Device* device);
  void shutdown();
  bool shader_dirty(const std::filesystem::path& path);
  rhi::PipelineHandleHolder create_graphics_pipeline(const rhi::GraphicsPipelineCreateInfo& cinfo);
  rhi::PipelineHandleHolder create_compute_pipeline(const rhi::ShaderCreateInfo& cinfo);
  bool compile_shader(const std::filesystem::path& path);
  void recompile_shaders();
  void recompile_shaders_no_lock();
  void replace_dirty_pipelines();

 private:
  std::filesystem::path get_shader_path(const std::string& relative_path, rhi::ShaderType type);

  std::unordered_set<std::filesystem::path> clean_shaders_;
  std::unordered_map<std::filesystem::path, HashT> path_to_existing_hash_;
  std::unordered_map<std::filesystem::path, uint64_t> last_write_times_;
  std::unordered_map<std::filesystem::path, std::unordered_set<uint64_t>> path_to_pipelines_using_;
  std::unordered_map<std::filesystem::path, std::vector<std::filesystem::path>>
      filepath_to_src_hlsl_includers_;
  std::mutex pipeline_recompile_mtx_;
  std::vector<rhi::PipelineHandle> pipeline_recompile_requests_;

  std::unordered_map<uint64_t, rhi::GraphicsPipelineCreateInfo> graphics_pipeline_cinfos_;
  std::unordered_map<uint64_t, rhi::ShaderCreateInfo> compute_pipeline_cinfos_;

  std::unordered_map<std::filesystem::path, std::vector<std::filesystem::path>>
      src_hlsl_to_dependencies_;
  std::filesystem::path shader_out_dir_;
  std::filesystem::path depfile_dir_;
  std::filesystem::path hashes_filepath_;
  rhi::Device* device_{};
  const std::filesystem::path hlsl_src_dir = std::filesystem::path("resources/shaders/hlsl");

  std::jthread file_watcher_thread_;
  std::mutex file_watch_mtx_;
  std::mutex compile_shaders_mtx_;
  std::condition_variable file_watch_cv_;
  bool running_{true};
};

}  // namespace gfx
