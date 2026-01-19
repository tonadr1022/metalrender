#include "ShaderManager.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <vector>

#include "Device.hpp"
#include "core/Hash.hpp"
#include "core/Logger.hpp"
#include "gfx/Pipeline.hpp"

namespace fs = std::filesystem;

using HashT = uint64_t;
namespace gfx {

namespace {

std::string shader_model_from_hlsl_path(const std::filesystem::path& path) {
  std::string shader_model_prefix;

  std::string filename = path.filename().string();
  auto first_dot = filename.find('.');
  auto second_dot = filename.find('.', first_dot + 1);

  if (first_dot == std::string::npos) {
    return "_6_7";
  }

  std::string shader_type = (second_dot == std::string::npos)
                                ? filename.substr(first_dot + 1)
                                : filename.substr(first_dot + 1, second_dot - first_dot - 1);

  if (shader_type == "vert") shader_model_prefix = "vs";
  if (shader_type == "frag") shader_model_prefix = "ps";
  if (shader_type == "mesh") shader_model_prefix = "ms";
  if (shader_type == "task") shader_model_prefix = "as";
  if (shader_type == "comp") shader_model_prefix = "cs";

  return shader_model_prefix + "_6_7";
}

const char* get_shader_type_string(rhi::ShaderType type) {
  const char* type_str{};
  switch (type) {
    case rhi::ShaderType::Fragment:
      type_str = "frag";
      break;
    case rhi::ShaderType::Vertex:
      type_str = "vert";
      break;
    case rhi::ShaderType::Mesh:
      type_str = "mesh";
      break;
    case rhi::ShaderType::Task:
      type_str = "task";
      break;
    case rhi::ShaderType::Compute:
      type_str = "comp";
      break;
    default:
      ASSERT(0);
      break;
  }
  return type_str;
}

std::string path_after_word(const std::filesystem::path& p, const char* word) {
  auto it = std::ranges::find(p, word);
  if (it == p.end()) return {};

  ++it;  // move past "hlsl"

  std::filesystem::path result;
  for (; it != p.end(); ++it) {
    result /= *it;
  }

  return result.generic_string();
}

HashT get_hash_for_shader_file(const fs::path& path, const std::vector<fs::path>& dep_paths) {
  auto result = static_cast<HashT>(fs::last_write_time(path).time_since_epoch().count());
  for (const auto& dep_path : dep_paths) {
    util::hash::hash_combine(
        result, static_cast<HashT>(fs::last_write_time(dep_path).time_since_epoch().count()));
  }
  return result;
}

struct FileAndDepPaths {
  fs::path file;
  std::vector<fs::path> deps;
};

FileAndDepPaths get_dep_filepaths(const fs::path& dep_filepath) {
  std::ifstream file(dep_filepath);
  FileAndDepPaths result;
  if (!file.is_open()) {
    LINFO("Failed to read file {}", dep_filepath.string());
    return result;
  }

  std::string token;
  while (file >> token) {
    if (token == "\\") {
      continue;
    }
    // Check for target definition (ends with :)
    if (token.back() == ':') {
      continue;
    }
    // Handle strictly attached backslash
    if (token.back() == '\\') {
      token.pop_back();
    }
    if (token.empty()) {
      continue;
    }
    if (result.file.empty()) {
      result.file = token;
    } else {
      result.deps.emplace_back(token);
    }
  }
  return result;
}

struct FileAndHash {
  fs::path file;
  HashT hash;
};

std::vector<FileAndHash> get_filehashes_from_file(const fs::path& hashfilepath) {
  std::ifstream file(hashfilepath);
  std::vector<FileAndHash> result;
  if (!file.is_open()) {
    LINFO("Failed to read file {}", hashfilepath.string());
    return result;
  }
  std::string token;
  int i = 0;
  FileAndHash curr;
  while (file >> token) {
    if (i % 2 == 0) {
      curr.file = std::move(token);
    } else {
      curr.hash = std::stoull(token);
      result.emplace_back(std::move(curr));
    }
    i++;
  }

  return result;
}

std::vector<FileAndHash> get_new_filehashes(const fs::path& dep_file_dir) {
  std::vector<FileAndHash> shader_hashes;
  for (const auto& entry : fs::recursive_directory_iterator(dep_file_dir)) {
    if (entry.is_regular_file()) {
      auto deps = get_dep_filepaths(entry.path());
      auto hash = get_hash_for_shader_file(deps.file, deps.deps);
      shader_hashes.emplace_back(deps.file, hash);
    }
  }
  return shader_hashes;
}

void write_file_hashes(const fs::path& out_path, std::span<FileAndHash> file_hashes) {
  std::ofstream file(out_path);
  for (const auto& hash : file_hashes) {
    file << hash.file.string() << ' ';
    file << hash.hash << '\n';
  }
}

[[maybe_unused]] void print_file_deps(const FileAndDepPaths& deps) {
  LINFO("File: {}", deps.file.string());
  for (const auto& dep : deps.deps) {
    LINFO("\t{}", dep.string());
  }
  LINFO("Hash {}", get_hash_for_shader_file(deps.file, deps.deps));
}

}  // namespace

void ShaderManager::init(rhi::Device* device, const std::filesystem::path&,
                         const std::filesystem::path& shader_out_dir) {
  device_ = device;
  shader_out_dir_ = shader_out_dir;
  depfile_dir_ = shader_out_dir_ / "deps";
  fs::create_directories(depfile_dir_);
  if (!fs::exists(depfile_dir_)) {
    LINFO("{} doesn't exist", depfile_dir_.string());
  }
  hashes_filepath_ = shader_out_dir_ / "deps.txt";
  std::unordered_map<fs::path, HashT> path_to_existing_hash;
  if (fs::exists(hashes_filepath_)) {
    auto existing_shader_hashes = get_filehashes_from_file(hashes_filepath_);
    path_to_existing_hash.reserve(existing_shader_hashes.size());
    for (const auto& hash : existing_shader_hashes) {
      path_to_existing_hash.emplace(hash.file, hash.hash);
    }
  }

  auto new_shader_hashes = get_new_filehashes(depfile_dir_);
  std::vector<fs::path> dirty_shaders;
  for (const auto& hash : new_shader_hashes) {
    if (!path_to_existing_hash.contains(hash.file) ||
        path_to_existing_hash.at(hash.file) != hash.hash) {
      compile_shader(hash.file);
    }
    clean_shaders_.insert(hash.file);
  }
}

void ShaderManager::shutdown() {
  auto new_hashes = get_new_filehashes(depfile_dir_);
  if (clean_shaders_.size()) {
    // LINFO("{} dirty shaders still, this shouldn't happen!", dirty_shaders_.size());
    // for (const auto& s : dirty_shaders_) {
    //   std::erase_if(new_hashes, [&](const FileAndHash& h) { return h.file == s; });
    // }
  }
  write_file_hashes(hashes_filepath_, new_hashes);
}

bool ShaderManager::shader_dirty(const std::filesystem::path& path) {
  return !clean_shaders_.contains(path);
}

rhi::PipelineHandleHolder ShaderManager::create_graphics_pipeline(
    const rhi::GraphicsPipelineCreateInfo& cinfo) {
  for (const auto& shader : cinfo.shaders) {
    if (shader.type == rhi::ShaderType::None) break;
    std::filesystem::path shader_source_path = get_shader_path(shader.path, shader.type);
    if (shader_dirty(shader_source_path)) {
      LINFO("need to cpi {}", shader_source_path.string());
      bool success = compile_shader(shader_source_path);
      if (!success) {
        return {};
      }
      clean_shaders_.insert(shader_source_path);
    }
  }
  return device_->create_graphics_pipeline_h(cinfo);
}

rhi::PipelineHandleHolder ShaderManager::create_compute_pipeline(
    const rhi::ShaderCreateInfo& cinfo) {
  auto shader_source_path = get_shader_path(cinfo.path, rhi::ShaderType::Compute);
  if (shader_dirty(shader_source_path)) {
    LINFO("need to cpi {}", shader_source_path.string());
    bool success = compile_shader(shader_source_path);
    if (!success) {
      return {};
    }
    clean_shaders_.insert(shader_source_path);
  }
  return device_->create_compute_pipeline_h(cinfo);
}

std::filesystem::path ShaderManager::get_shader_path(const std::string& relative_path,
                                                     rhi::ShaderType type) {
  static const fs::path shader_src_dir = fs::path("resources") / "shaders" / "hlsl";
  return (shader_src_dir / relative_path)
      .concat(".")
      .concat(get_shader_type_string(type))
      .concat(".hlsl");
}

bool ShaderManager::compile_shader(const std::filesystem::path& path) {
  // TODO: handle spirv
  auto shader_model = shader_model_from_hlsl_path(path);
  LINFO("compiling {}", path.string(), shader_model);
  auto relative = path_after_word(path, "hlsl");
  auto out_filepath =
      (fs::path("resources/shader_out/metal") / relative).replace_extension(".dxil");
  auto dep_filepath = (fs::path("resources/shader_out/deps") / relative).replace_extension(".dxil");
  fs::create_directories(out_filepath.parent_path());
  fs::create_directories(dep_filepath.parent_path());
  std::string cmd1 =
      std::format("dxc {} -Fo {} -T {} -E main -Zi -Qembed_debug -Qsource_in_debug_module",
                  path.string(), out_filepath.string(), shader_model);
  std::system(cmd1.c_str());
  std::string cmd2 = std::format("dxc {} -T {} -E main -MF {}", path.string(), shader_model,
                                 dep_filepath.string());
  std::system(cmd2.c_str());
  auto metallib_path =
      (fs::path("resources/shader_out/metal") / relative).replace_extension(".metallib");
  std::string metallib_compile_args =
      std::format("metal-shaderconverter {} -o {}", out_filepath.string(), metallib_path.string());
  std::system(metallib_compile_args.c_str());
  return true;
}

}  // namespace gfx
