#include <algorithm>
#include <cxxopts.hpp>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "shader_compiler/ShaderCompiler.hpp"

namespace fs = std::filesystem;

namespace {

constexpr const char* kExeName = "teng-shaderc";

bool is_entry_point_hlsl(const fs::path& path) {
  static constexpr std::string_view kSuffixes[] = {".vert.hlsl", ".frag.hlsl", ".comp.hlsl",
                                                   ".mesh.hlsl", ".task.hlsl"};
  const std::string filename = path.filename().string();
  const std::string_view n = filename;
  return std::ranges::any_of(kSuffixes, [&](std::string_view suf) { return n.ends_with(suf); });
}

bool set_project_root(const fs::path& root) {
  std::error_code ec;
  fs::current_path(root, ec);
  if (ec) {
    std::cerr << kExeName << ": cannot chdir to " << root << ": " << ec.message() << '\n';
    return false;
  }
  return true;
}

bool looks_like_project(const fs::path& dir) {
  return fs::is_directory(dir / "resources" / "shaders" / "hlsl");
}

fs::path find_project_root_from(fs::path start) {
  start = fs::absolute(start);
  for (int i = 0; i < 32 && !start.empty(); ++i) {
    if (looks_like_project(start)) return start;
    auto parent = start.parent_path();
    if (parent == start) break;
    start = std::move(parent);
  }
  return {};
}

}  // namespace

int main(int argc, char** argv) {
  cxxopts::Options options(argv[0], "Compile HLSL shaders to SPIR-V / MSL");
  // clang-format off
  options.add_options()
    ("project-root", "Use this directory as the project root (must contain resources/shaders/hlsl)",
     cxxopts::value<std::string>())
    ("all", "Compile every entry-point *.vert|frag|comp|mesh|task.hlsl under resources/shaders/hlsl",
     cxxopts::value<bool>()->default_value("false"))
    ("h,help", "Show this help")
    ("shaders", "HLSL entry-point files to compile", cxxopts::value<std::vector<std::string>>());
  // clang-format on
  options.parse_positional({"shaders"});
  options.positional_help("<shader.hlsl> [more.hlsl ...]");

  cxxopts::ParseResult result;
  try {
    result = options.parse(argc, argv);
  } catch (const cxxopts::exceptions::exception& e) {
    std::cerr << argv[0] << ": " << e.what() << '\n';
    std::cout << options.help() << '\n';
    return 1;
  }

  if (result.contains("help")) {
    std::cout << options.help() << '\n';
    return 0;
  }

  const bool compile_all = result["all"].as<bool>();
  const std::vector<std::string> positional =
      result.contains("shaders") ? result["shaders"].as<std::vector<std::string>>() : std::vector<std::string>{};

  if (compile_all && !positional.empty()) {
    std::cerr << kExeName << ": --all cannot be combined with shader paths\n";
    std::cerr << options.help() << '\n';
    return 2;
  }
  if (!compile_all && positional.empty()) {
    std::cerr << options.help() << '\n';
    return 2;
  }

  fs::path project_root;
  if (result.contains("project-root")) {
    project_root = result["project-root"].as<std::string>();
  }

  if (!project_root.empty()) {
    if (!set_project_root(fs::absolute(project_root))) return 1;
  } else {
    const fs::path found = find_project_root_from(fs::current_path());
    if (found.empty()) {
      std::cerr << kExeName << ": could not find resources/shaders/hlsl; pass --project-root\n";
      return 1;
    }
    if (!set_project_root(found)) return 1;
  }

  std::vector<fs::path> to_compile;
  if (compile_all) {
    const fs::path hlsl_root = "resources/shaders/hlsl";
    if (!fs::exists(hlsl_root)) {
      std::cerr << kExeName << ": missing " << hlsl_root << '\n';
      return 1;
    }
    for (const auto& e : fs::recursive_directory_iterator(hlsl_root)) {
      if (!e.is_regular_file()) continue;
      const fs::path& p = e.path();
      if (p.extension() != ".hlsl") continue;
      if (!is_entry_point_hlsl(p)) continue;
      to_compile.push_back(fs::absolute(p));
    }
    std::ranges::sort(to_compile);
  } else {
    to_compile.reserve(positional.size());
    for (const auto& p_str : positional) {
      to_compile.push_back(fs::absolute(p_str));
    }
  }

  int exit_code = 0;
  for (const fs::path& p : to_compile) {
    if (!fs::exists(p)) {
      std::cerr << kExeName << ": not found: " << p << '\n';
      exit_code = 1;
      continue;
    }
    std::string err;
    if (!teng::shader_compiler::compile_hlsl_file(p, {}, &err)) {
      std::cerr << kExeName << ": " << err << '\n';
      exit_code = 1;
    }
  }

  return exit_code;
}
