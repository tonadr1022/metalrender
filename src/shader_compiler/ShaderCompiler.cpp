#include "shader_compiler/ShaderCompiler.hpp"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <string>
#include <vector>

#ifndef _WIN32
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace teng::shader_compiler {

namespace fs = std::filesystem;

std::string path_after_word(const fs::path& p, const char* word) {
  auto it = std::ranges::find(p, word);
  if (it == p.end()) return {};

  ++it;

  fs::path result;
  for (; it != p.end(); ++it) {
    result /= *it;
  }

  return result.generic_string();
}

std::string shader_model_from_hlsl_path(const fs::path& path) {
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

#ifndef _WIN32

int run_executable(const char* executable, const std::vector<std::string>& args) {
  std::vector<std::string> storage;
  storage.reserve(args.size() + 2);
  storage.emplace_back(executable);
  for (const auto& a : args) {
    storage.push_back(a);
  }

  std::vector<char*> argv;
  argv.reserve(storage.size() + 1);
  for (auto& s : storage) {
    argv.push_back(s.data());
  }
  argv.push_back(nullptr);

  pid_t pid{};
  const int spawn_err = posix_spawnp(&pid, executable, nullptr, nullptr, argv.data(), environ);
  if (spawn_err != 0) {
    return -1;
  }

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    return -1;
  }
  if (!WIFEXITED(status)) {
    return -1;
  }
  return WEXITSTATUS(status);
}

#else

int run_executable(const char* executable, const std::vector<std::string>& args) {
  std::string cmd = executable;
  for (const auto& a : args) {
    cmd += ' ';
    cmd += a;
  }
  return std::system(cmd.c_str());
}

#endif

bool compile_hlsl_file(const fs::path& source_hlsl, const CompileOptions& options,
                       std::string* error) {
  if (source_hlsl.empty()) {
    if (error) *error = "empty shader path";
    return false;
  }

  const std::string shader_model = shader_model_from_hlsl_path(source_hlsl);
  const std::string relative = path_after_word(source_hlsl, "hlsl");
  if (relative.empty()) {
    if (error) {
      *error = std::format("path must contain an 'hlsl' segment: {}", source_hlsl.string());
    }
    return false;
  }

  auto out_filepath =
      (fs::path("resources/shader_out/metal") / relative).replace_extension(".dxil");
  auto dep_filepath = (fs::path("resources/shader_out/deps") / relative).replace_extension(".d");
  fs::create_directories(out_filepath.parent_path());
  fs::create_directories(dep_filepath.parent_path());

  const std::string path_str = source_hlsl.string();

  if (options.emit_dxil) {
    std::vector<std::string> args = {
        path_str,          "-Fo",           out_filepath.string(), "-T", shader_model, "-E", "main",
        "-rootsig-define", "ROOT_SIGNATURE"};
    if (options.debug_enabled) {
      args.insert(args.end(), {"-Zi", "-Qembed_debug", "-Qsource_in_debug_module"});
    }
    if (run_executable("dxc", args) != 0) {
      if (error) *error = std::format("dxc (dxil) failed for {}", path_str);
      return false;
    }
  }

  if (options.emit_spirv) {
    auto spirv_path = out_filepath;
    spirv_path.replace_extension(".spv");
    std::vector<std::string> args = {
        path_str, "-Fo", spirv_path.string(), "-T", shader_model, "-E", "main", "-spirv",
        "-fspv-target-env=vulkan1.3", "-fspv-extension=SPV_NV_mesh_shader",
        "-fspv-extension=SPV_EXT_descriptor_indexing", "-fvk-use-dx-layout", "-fvk-u-shift", "1000",
        "0", "-fvk-t-shift", "2000", "0",
        //  "-rootsig-define",
        //  "ROOT_SIGNATURE",
        "-D", "VULKAN"};
    if (options.debug_enabled) {
      args.insert(args.end(), {"-Zi", "-Qembed_debug", "-Qsource_in_debug_module"});
    }
    if (run_executable("dxc", args) != 0) {
      if (error) *error = std::format("dxc (spirv) failed for {}", path_str);
      return false;
    }
  }

  if (options.emit_depfile) {
    std::vector<std::string> args = {path_str, "-T",  shader_model,         "-E",
                                     "main",   "-MF", dep_filepath.string()};
    if (run_executable("dxc", args) != 0) {
      if (error) *error = std::format("dxc (-MF) failed for {}", path_str);
      return false;
    }
  }

  if (options.emit_metallib) {
    auto metallib_path =
        (fs::path("resources/shader_out/metal") / relative).replace_extension(".metallib");
    const bool output_reflection = true;
    std::vector<std::string> args = {out_filepath.string(), "-o", metallib_path.string()};
    if (output_reflection) {
      auto reflection_path = metallib_path;
      reflection_path.replace_extension(".json");
      args.emplace_back("--output-reflection-file");
      args.emplace_back(reflection_path.string());
    }
    if (run_executable("metal-shaderconverter", args) != 0) {
      if (error) *error = std::format("metal-shaderconverter failed for {}", path_str);
      return false;
    }
  }

  return true;
}

}  // namespace teng::shader_compiler
