#include "ShaderManager.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

#include "core/Logger.hpp"

namespace fs = std::filesystem;

namespace gfx {

namespace {

struct PathAndWriteTime {
  fs::path path;
  std::chrono::system_clock::time_point last_write_time;
};

std::vector<PathAndWriteTime> get_last_written_times(const fs::path& dir) {
  std::vector<PathAndWriteTime> result;
  for (const auto& path : fs::recursive_directory_iterator(dir)) {
    if (!path.is_regular_file()) {
      continue;
    }
    fs::file_time_type ftime = path.last_write_time();
    auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        std::chrono::file_clock::to_sys(ftime));
    result.emplace_back(path.path(), system_time);
  }
  return result;
}

uint64_t get_hash_for_shader_file() {}

std::vector<std::filesystem::path> get_dep_filepaths(const fs::path& depfile_dir) {
  std::ifstream file(depfile_dir);
  std::vector<std::filesystem::path> result;
  if (!file.is_open()) {
    LINFO("Failed to read file {}", depfile_dir.string());
    return result;
  }
  // read the file name

  // clang-format off
  /*
  Example
resources/shader_out/metal/basic_indirect.frag.dxil: resources/shaders/hlsl/basic_indirect.frag.hlsl \
 resources/shaders/hlsl/root_sig.h \
 resources/shaders/hlsl/material.h \
 resources/shaders/hlsl/../shader_core.h \
 resources/shaders/hlsl/shared_basic_indirect.h

    */
  // clang-format on
  return result;
}

}  // namespace

void ShaderManager::init(const fs::path& shader_dir, const fs::path& shader_out_dir) {
  fs::path dep_file_dir = shader_out_dir / "deps";
  get_dep_filepaths(dep_file_dir);
  auto last_write_times = get_last_written_times(shader_dir);
  for (const auto& item : last_write_times) {
    auto time = std::chrono::system_clock::to_time_t(item.last_write_time);
    std::cout << item.path << " " << std::put_time(std::localtime(&time), "%F %T") << '\n';
  }
}

}  // namespace gfx
