#include "ShaderManager.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <span>
#include <tracy/Tracy.hpp>
#include <vector>

#include "core/Config.hpp"
#include "core/EAssert.hpp"
#include "core/Hash.hpp"
#include "core/Logger.hpp"
#include "gfx/rhi/Device.hpp"

namespace TENG_NAMESPACE {

namespace fs = std::filesystem;

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

  ++it;

  std::filesystem::path result;
  for (; it != p.end(); ++it) {
    result /= *it;
  }

  return result.generic_string();
}

ShaderManager::HashT get_hash_for_shader_file(const fs::path& path,
                                              const std::vector<fs::path>& dep_paths) {
  using HashT = ShaderManager::HashT;
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
  ShaderManager::HashT hash;
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

void ShaderManager::init(rhi::Device* device, const Options& options) {
  ZoneScoped;
  options_ = options;
  device_ = device;
  shader_out_dir_ = fs::path("resources/shader_out");
  depfile_dir_ = shader_out_dir_ / "deps";

  fs::create_directories(depfile_dir_);
  if (!fs::exists(depfile_dir_)) {
    LINFO("{} doesn't exist", depfile_dir_.string());
  }

  hashes_filepath_ = shader_out_dir_ / "deps.txt";

  if (fs::exists(hashes_filepath_)) {
    auto existing_shader_hashes = get_filehashes_from_file(hashes_filepath_);
    path_to_existing_hash_.reserve(existing_shader_hashes.size());
    for (const auto& hash : existing_shader_hashes) {
      path_to_existing_hash_.emplace(hash.file, hash.hash);
    }
  }

  auto new_shader_hashes = get_new_filehashes(depfile_dir_);
  std::vector<fs::path> dirty_shaders;
  for (const auto& hash : new_shader_hashes) {
    if (!path_to_existing_hash_.contains(hash.file) ||
        path_to_existing_hash_.at(hash.file) != hash.hash) {
      compile_shader(hash.file);
    }
    clean_shaders_.insert(hash.file);
  }

  ASSERT(fs::exists(std::filesystem::current_path() / hlsl_src_dir));
  for (const auto& entry : fs::recursive_directory_iterator(hlsl_src_dir)) {
    if (!entry.is_regular_file()) continue;
    last_write_times_.emplace(entry.path(), entry.last_write_time().time_since_epoch().count());
    if (entry.path().extension() != ".hlsl") {
      continue;
    }
    auto depfile_filepath =
        (depfile_dir_ / path_after_word(entry.path(), "hlsl")).replace_extension(".d");
    if (!std::filesystem::exists(depfile_filepath)) {
      continue;
    }
    auto deps = get_dep_filepaths(depfile_filepath);
    if (deps.file.empty()) {
      continue;
    }
    for (const auto& d : deps.deps) {
      filepath_to_src_hlsl_includers_[d].emplace_back(deps.file);
    }
  }

  std::vector<std::filesystem::path> dirty_paths;
  check_and_recompile(dirty_paths);

  file_watcher_thread_ = std::jthread{[this]() {
    std::vector<std::filesystem::path> dirty_paths;
    dirty_paths.reserve(20);
    while (true) {
      std::unique_lock lk(file_watch_mtx_);

      // wake up early only if shutdown
      file_watch_cv_.wait_for(lk, std::chrono::milliseconds(100), [this] { return !running_; });

      if (!running_) return;

      lk.unlock();

      check_and_recompile(dirty_paths);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }};
}

void ShaderManager::recompile_shaders() {
  std::lock_guard l(compile_shaders_mtx_);
  recompile_shaders_no_lock();
}

void ShaderManager::recompile_shaders_no_lock() {
  for (const auto& entry : fs::recursive_directory_iterator(hlsl_src_dir)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".hlsl") {
      continue;
    }
    auto depfile_filepath =
        (depfile_dir_ / path_after_word(entry.path(), "hlsl")).replace_extension(".d");
    auto deps = get_dep_filepaths(depfile_filepath);
    auto hash = get_hash_for_shader_file(deps.file, deps.deps);
    if (!path_to_existing_hash_.contains(entry.path()) ||
        path_to_existing_hash_.at(entry.path()) != hash) {
      compile_shader(entry.path());
    }
  }
}

void ShaderManager::shutdown() {
  {
    std::lock_guard l(file_watch_mtx_);
    running_ = false;
  }
  file_watch_cv_.notify_one();

  ASSERT(file_watcher_thread_.joinable());
  file_watcher_thread_.join();

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
  std::array<std::filesystem::path, 10> paths;
  size_t shader_cnt = 0;
  for (const auto& shader : cinfo.shaders) {
    if (shader.type == rhi::ShaderType::None) break;
    paths[shader_cnt] = get_shader_path(shader.path, shader.type);
    const auto& shader_source_path = paths[shader_cnt];
    if (shader_dirty(shader_source_path)) {
      bool success = compile_shader(shader_source_path);
      if (!success) {
        return {};
      }
      clean_shaders_.insert(shader_source_path);
    }
    shader_cnt++;
  }
  auto handle = device_->create_graphics_pipeline_h(cinfo);
  for (size_t i = 0; i < shader_cnt; i++) {
    auto& path = paths[i];
    path_to_pipelines_using_[path].emplace(handle.handle.to64());
  }
  graphics_pipeline_cinfos_.emplace(handle.handle.to64(), cinfo);
  return handle;
}

rhi::PipelineHandleHolder ShaderManager::create_compute_pipeline(
    const rhi::ShaderCreateInfo& cinfo) {
  auto shader_source_path = get_shader_path(cinfo.path, rhi::ShaderType::Compute);
  if (shader_dirty(shader_source_path)) {
    bool success = compile_shader(shader_source_path);
    if (!success) {
      return {};
    }
    clean_shaders_.insert(shader_source_path);
  }
  auto handle = device_->create_compute_pipeline_h(cinfo);
  compute_pipeline_cinfos_.emplace(handle.handle.to64(), cinfo);
  return handle;
}

std::filesystem::path ShaderManager::get_shader_path(const std::string& relative_path,
                                                     rhi::ShaderType type) {
  const fs::path shader_src_dir = fs::path("resources") / "shaders" / "hlsl";
  return (shader_src_dir / relative_path)
      .concat(".")
      .concat(get_shader_type_string(type))
      .concat(".hlsl");
}

bool ShaderManager::compile_shader(const std::filesystem::path& path, bool debug_enabled) {
  // TODO: handle spirv
  auto shader_model = shader_model_from_hlsl_path(path);
  ASSERT(!path.empty());
  LINFO("compiling {} {}", path.string(), shader_model);
  auto relative = path_after_word(path, "hlsl");
  auto out_filepath =
      (fs::path("resources/shader_out/metal") / relative).replace_extension(".dxil");
  auto dep_filepath = (fs::path("resources/shader_out/deps") / relative).replace_extension(".d");
  fs::create_directories(out_filepath.parent_path());
  fs::create_directories(dep_filepath.parent_path());
  std::string compile_dxil =
      std::format("dxc {} -Fo {} -T {} -E main {}", path.string(), out_filepath.string(),
                  shader_model, debug_enabled ? "-Zi -Qembed_debug -Qsource_in_debug_module" : "");
  if (std::system(compile_dxil.c_str())) {
    LINFO("dxc failed for {}", path.string());
    return false;
  }

  if (has_flag(options_.targets, rhi::ShaderTarget::Spirv)) {  // spirv
    auto spirv_path = out_filepath;
    spirv_path.replace_extension(".spv");
    auto compile_spirv = std::format(
        "dxc {} -Fo {} -T {} -E main {}  -fspv-preserve-bindings -fspv-reflect -spirv "
        "-fspv-target-env=vulkan1.3",
        path.string(), spirv_path.string(), shader_model,
        debug_enabled ? "-Zi -Qembed_debug -Qsource_in_debug_module" : "");
    if (std::system(compile_spirv.c_str())) {
      LINFO("dxc spirv failed for {}", compile_spirv);
      return false;
    }
  }

  {  // write shader deps
    std::string write_shader_dependencies_cmd = std::format(
        "dxc {} -T {} -E main -MF {}", path.string(), shader_model, dep_filepath.string());
    if (std::system(write_shader_dependencies_cmd.c_str())) {
      LINFO("dxc dep-generation failed for {}", path.string());
      return false;
    }
  }

  if (has_flag(options_.targets, rhi::ShaderTarget::MSL)) {
    auto metallib_path =
        (fs::path("resources/shader_out/metal") / relative).replace_extension(".metallib");
    bool output_reflection = true;

    std::string output_reflection_arg;
    if (output_reflection) {
      auto reflection_path = metallib_path;
      output_reflection_arg =
          output_reflection
              ? "--output-reflection-file " + reflection_path.replace_extension(".json").string()
              : "";
    }

    std::string metallib_compile_args =
        std::format("metal-shaderconverter {} -o {} {}", out_filepath.string(),
                    metallib_path.string(), output_reflection_arg);
    if (std::system(metallib_compile_args.c_str())) {
      LINFO("metal-shaderconverter metallib failed for {}", path.string());
      return false;
    }
  }

  // update hash
  auto deps = get_dep_filepaths(dep_filepath);
  auto hash = get_hash_for_shader_file(deps.file, deps.deps);
  if (path_to_existing_hash_.contains(path)) {
    LINFO("Updating hash {} {}", path.string(), hash);
  } else {
    LINFO("Adding hash {} {}", path.string(), hash);
  }
  path_to_existing_hash_[path] = hash;

  return true;
}

void ShaderManager::replace_dirty_pipelines() {
  std::lock_guard l(pipeline_recompile_mtx_);
  for (const auto& pipeline_handle : pipeline_recompile_requests_) {
    auto gpso_it = graphics_pipeline_cinfos_.find(pipeline_handle.to64());
    if (gpso_it != graphics_pipeline_cinfos_.end()) {
      bool success =
          device_->replace_pipeline(rhi::PipelineHandle{pipeline_handle}, gpso_it->second);
      LINFO("replaced graphics pipeline {} {}", pipeline_handle.to64(), success);
      continue;
    }
    auto cpso_it = compute_pipeline_cinfos_.find(pipeline_handle.to64());
    if (cpso_it != compute_pipeline_cinfos_.end()) {
      bool success =
          device_->replace_compute_pipeline(rhi::PipelineHandle{pipeline_handle}, cpso_it->second);
      LINFO("replaced compute pipeline {} {}", pipeline_handle.to64(), success);
      continue;
    }
  }
  pipeline_recompile_requests_.clear();
}

}  // namespace gfx

}  // namespace TENG_NAMESPACE

void teng::gfx::ShaderManager::check_and_recompile(
    std::vector<std::filesystem::path>& dirty_paths) {
  std::lock_guard lk(compile_shaders_mtx_);
  dirty_paths.clear();
  for (const auto& entry : fs::recursive_directory_iterator(hlsl_src_dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    auto last_write_time = entry.last_write_time().time_since_epoch().count();
    const auto& path = entry.path();
    auto it = last_write_times_.find(path);
    if (it == last_write_times_.end() || it->second < last_write_time ||
        (path.extension() == ".hlsl" &&
         !std::filesystem::exists(
             (fs::path("resources/shader_out/metal") / path_after_word(path, "hlsl"))
                 .replace_extension(".dxil")))) {
      // TODO: cursed
      if (!path.string().contains("root_sig")) {
        LINFO("dirty shader detected: {}", path.string());
        dirty_paths.emplace_back(path);
        last_write_times_[path] = last_write_time;
      }
    }
  }

  auto recompile = [this](const fs::path& path) {
    bool success = compile_shader(path);
    if (!success) {
      LINFO("Failed to compile shader {}", path.string());
      return;
    }
    clean_shaders_.insert(path);
    // recompile the pipelines using the shader
    auto it = path_to_pipelines_using_.find(path);
    if (it != path_to_pipelines_using_.end()) {
      for (const auto& pipeline_handle : it->second) {
        std::lock_guard l(pipeline_recompile_mtx_);
        pipeline_recompile_requests_.emplace_back(pipeline_handle);
      }
    }
  };

  for (const auto& dirty_path : dirty_paths) {
    auto it = filepath_to_src_hlsl_includers_.find(dirty_path);
    if (dirty_path.extension() == ".hlsl") {
      recompile(dirty_path);
    }
    if (it != filepath_to_src_hlsl_includers_.end()) {
      for (const auto& includer_path : it->second) {
        recompile(includer_path);
      }
    }
  }
}
