#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace teng::shader_compiler {

/// Path segments after the directory named `word` (e.g. "hlsl" -> path under resources/shaders/hlsl).
[[nodiscard]] std::string path_after_word(const std::filesystem::path& p, const char* word);

[[nodiscard]] std::string shader_model_from_hlsl_path(const std::filesystem::path& path);

struct CompileOptions {
  bool debug_enabled = false;
  bool emit_dxil = true;
  bool emit_spirv = true;
  bool emit_depfile = true;
  bool emit_metallib = true;
};

/// Invokes dxc and metal-shaderconverter like ShaderManager did (cwd must be project root).
/// On failure, fills `error` with a short message if non-null.
[[nodiscard]] bool compile_hlsl_file(const std::filesystem::path& source_hlsl,
                                     const CompileOptions& options = {},
                                     std::string* error = nullptr);

/// Runs `executable` (resolved via PATH) with given args; argv[0] is set to `executable`.
/// Returns exit status (0 = success), or -1 if spawn/wait failed.
[[nodiscard]] int run_executable(const char* executable, const std::vector<std::string>& args);

}  // namespace teng::shader_compiler
