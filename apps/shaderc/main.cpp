#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "shader_compiler/ShaderCompiler.hpp"

namespace fs = std::filesystem;

namespace {

constexpr const char* kExeName = "teng-shaderc";

void usage(const char* argv0) {
  std::cerr << "usage: " << argv0
            << " [--project-root <dir>] (--all | <shader.hlsl> [more.hlsl ...])\n";
}

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
  std::vector<std::string> positional;
  fs::path project_root;
  bool compile_all = false;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--project-root" && i + 1 < argc) {
      project_root = argv[++i];
    } else if (a == "--all") {
      compile_all = true;
    } else if (a == "-h" || a == "--help") {
      usage(argv[0]);
      return 0;
    } else {
      positional.push_back(std::move(a));
    }
  }

  if (compile_all && !positional.empty()) {
    std::cerr << kExeName << ": --all cannot be combined with shader paths\n";
    usage(argv[0]);
    return 2;
  }
  if (!compile_all && positional.empty()) {
    usage(argv[0]);
    return 2;
  }

  if (!project_root.empty()) {
    if (!set_project_root(fs::absolute(project_root))) return 1;
  } else {
    fs::path found = find_project_root_from(fs::current_path());
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
