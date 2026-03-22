#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "shader_compiler/ShaderCompiler.hpp"

namespace fs = std::filesystem;

namespace {

constexpr const char* kExeName = "teng-shaderc";

void usage(const char* argv0) {
  std::cerr << "usage: " << argv0 << " [--project-root <dir>] <shader.hlsl> [more.hlsl ...]\n";
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

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--project-root" && i + 1 < argc) {
      project_root = argv[++i];
    } else if (a == "-h" || a == "--help") {
      usage(argv[0]);
      return 0;
    } else {
      positional.push_back(std::move(a));
    }
  }

  if (positional.empty()) {
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

  int exit_code = 0;
  for (const auto& p_str : positional) {
    fs::path p = fs::absolute(p_str);
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
